#include <win_io/detail/read_directory_changes.h>

#include <string_view>
#include <cstdio>
#include <Windows.h>

using namespace wi;

struct Options
{
    bool watch_sub_tree = true; // recursive by default
    DWORD notify_filter = 0;
    const wchar_t* directory = L".";
    bool verbose = false;
    bool print_time = false;
};

struct FlagMeta
{
    const DWORD flag;
    const wchar_t* const arg;
    const wchar_t* const description;
};

static const FlagMeta k_notify_filters[] =
{
    {FILE_NOTIFY_CHANGE_FILE_NAME,   L"f", L"File name change (renaming, creating, or deleting a file)"},
    {FILE_NOTIFY_CHANGE_DIR_NAME,    L"d", L"Directory name change (creating or deleting a directory)"},
    {FILE_NOTIFY_CHANGE_ATTRIBUTES,  L"a", L"Attribute change"},
    {FILE_NOTIFY_CHANGE_SIZE,        L"s", L"File size change"},
    {FILE_NOTIFY_CHANGE_LAST_WRITE,  L"w", L"Last write time change of files"},
    {FILE_NOTIFY_CHANGE_LAST_ACCESS, L"t", L"Last access time change of files"},
    {FILE_NOTIFY_CHANGE_CREATION,    L"c", L"Creation time change of files"},
    {FILE_NOTIFY_CHANGE_SECURITY,    L"s", L"Security descriptor change"},
};

static const FlagMeta k_actions[] =
{
    {FILE_ACTION_ADDED,             L"a", L"File was added to the directory"},
    {FILE_ACTION_REMOVED,           L"r", L"File was removed from the directory"},
    {FILE_ACTION_MODIFIED,          L"m", L"File was modified (time stamp or attributes)"},
    {FILE_ACTION_RENAMED_OLD_NAME,  L"o", L"File was renamed and this is the old name"},
    {FILE_ACTION_RENAMED_NEW_NAME,  L"n", L"File was renamed and this is the new name"},
};

static const wchar_t k_flags_start        = L'+';
static const wchar_t k_opt_NO_recursive[] = L".";
static const wchar_t k_opt_verbose[]      = L"v";

const struct
{
    const wchar_t* const arg;
    const wchar_t* const description;
} k_args[] =
{
    {k_opt_NO_recursive, L"Disable recursive directory watch"},
    {k_opt_verbose,      L"Verbose output to stderr (errors + options)"},
};

static const wchar_t* GetActionArg(DWORD action_id)
{
    for (const auto& action : k_actions)
    {
        if (action.flag == action_id)
            return action.arg;
    }
    fprintf(stderr, "Unexpected action: %u\n"
        , unsigned(action_id));
    abort();
}

static DWORD GetFilterFromString(std::wstring_view str)
{
    for (const auto& filter : k_notify_filters)
    {
        // std::string_view operator==() for const char* is missing
        if (str.compare(filter.arg) == 0)
            return filter.flag;
    }
    fprintf(stderr, "Unexpected filter string: %.*S\n"
        , int(str.size())
        , str.data());
    abort();
}

static std::wstring_view GetFileNameOnly(std::wstring_view exe_path)
{
    const std::size_t pos = exe_path.find_last_of(L"\\/");
    if (pos == exe_path.npos)
        return exe_path;
    return exe_path.substr(pos + 1);
}

static void LogDirectoryChange(
      const Options& options
    , const DirectoryChange& change
    , const char* time_str)
{
    if (options.print_time && time_str)
        fprintf(stdout, "[%s] ", time_str);

    fprintf(stdout, "%C%S %.*S\n"
        , k_flags_start
        , GetActionArg(change.action)
        , int(change.name.size())
        , change.name.data());
}

static void LogChanges(const Options& options
    , const DirectoryChangesRange& range
    , std::chrono::system_clock::time_point time)
{
    char time_buffer[1024]; // uninitialized
    const char* time_str = nullptr;
    if (options.print_time)
    {
        const time_t c_time = std::chrono::system_clock::to_time_t(time);
        struct tm tm;
        const errno_t e = localtime_s(&tm, &c_time);
        if (e)
            abort();
        if (strftime(time_buffer, std::size(time_buffer), "%H:%M:%S", &tm) == 0)
            abort();
        time_str = time_buffer;
    }

    for (const DirectoryChange& change : range)
        LogDirectoryChange(options, change, time_str);
    fflush(stdout);
}

static int LogError(const Options& options
    , const char* message
    , const std::error_code& ec = std::error_code())
{
    if (!options.verbose)
        return -1;
    if (!ec)
        fprintf(stderr, "%s\n", message);
    else
        fprintf(stderr, "%s: %s\n", message, ec.message().c_str());
    return -1;
}

static void LogUnexpectedPortChanges(const Options& options, const DirectoryChangesResults& results)
{
    if (!options.verbose)
        return;

    if (const PortEntry* port_changes = results.port_changes())
    {
        fprintf(stderr, "Unexpected I/O port change. %#llx key, %#llx bytes\n"
            , std::uint64_t(port_changes->completion_key)
            , std::uint64_t(port_changes->bytes_transferred));
    }
    else if (!results.directory_changes())
        fprintf(stderr, "I/O port change without directory change?\n");

    fprintf(stderr, "Unexpected port changes\n");
    abort();
}

static int WatchForever(const Options& options)
{
    std::error_code ec;
    std::optional<IoCompletionPort> port = IoCompletionPort::make(1, ec);
    if (!port)
        return LogError(options, "Failed to create IoCompletionPort", ec);

    DWORD buffer[32 * 1024];
    std::optional<DirectoryChanges> dir_changes = DirectoryChanges::make(
        options.directory
        , buffer
        , sizeof(buffer) // in bytes
        , options.watch_sub_tree
        , options.notify_filter
        , *port
        , 1/*dir key*/
        , ec);
    if (!dir_changes)
        return LogError(options, "Failed to create DirectoryChanges", ec);

    while (true)
    {
        dir_changes->start_watch(ec);
        if (ec)
        {
            LogError(options, "Failed to start watching", ec);
            continue;
        }
        const DirectoryChangesResults results = dir_changes->get(ec);
        if (ec)
        {
            LogError(options, "Failed to get changes", ec);
            continue;
        }
        std::chrono::system_clock::time_point time;
        if (options.print_time)
            time = std::chrono::system_clock::now();
        const DirectoryChangesRange* changes = results.directory_changes();
        if (!changes)
        {
            LogUnexpectedPortChanges(options, results);
            continue;
        }
        LogChanges(options, *changes, time);
    }
    return 0;
}

static void LogOptions(std::wstring_view exe_path, const Options& options)
{
    const std::wstring_view exe_name = GetFileNameOnly(exe_path);
    fprintf(stderr, "(%s) %.*S %S %C"
        , options.watch_sub_tree ? "recursive" : "non-recursive"
        , int(exe_name.size())
        , exe_name.data()
        , options.directory
        , k_flags_start);
    if (options.verbose)
        fprintf(stderr, "%S", k_opt_verbose);
    if (!options.watch_sub_tree)
        fprintf(stderr, "%S", k_opt_NO_recursive);
    for (const FlagMeta& filter : k_notify_filters)
    {
        if ((options.notify_filter & filter.flag) == filter.flag)
            fprintf(stderr, "%S", filter.arg);
    }
    fprintf(stderr, "\n");
}

static bool ParseCommandLine(int argc, wchar_t* argv[], Options& options)
{
    if (argc < 2)
    {
        fprintf(stderr, "Too few arguments\n");
        return false;
    }

    options.directory = argv[1];
    options.notify_filter = 0; // nothing
    options.watch_sub_tree = true;

    bool enable_all_filters = true;
    if (argc >= 4)
        options.print_time = (wcscmp(argv[3], L"time") == 0);

    if (argc >= 3)
    {
        const std::wstring_view flags = argv[2];
        if ((flags.size() < 1)
            || (flags.front() != k_flags_start))
        {
            fprintf(stderr, "Invalid flags: %.*S\n"
                , int(flags.size()), flags.data());
            return false;
        }
        for (wchar_t filter_char : flags.substr(1)/*no + prefix*/)
        {
            const std::wstring_view filter_str(&filter_char, 1);
            if (filter_str.compare(k_opt_NO_recursive) == 0)
                options.watch_sub_tree = false;
            else if (filter_str.compare(k_opt_verbose) == 0)
                options.verbose = true;
            else
            {
                options.notify_filter |= GetFilterFromString(filter_str);
                enable_all_filters = false;
            }
        }
    }

    if (enable_all_filters)
    {
        for (const auto& filter : k_notify_filters)
            options.notify_filter |= filter.flag;
    }
    return true;
}

static int PrettyPrintHelp(std::wstring_view exe_path)
{
    const std::wstring_view exe_name = GetFileNameOnly(exe_path);
    fprintf(stdout
        , "Help:\n\n  %.*S <directory> %C<flags> [time]\n\n"
        , int(exe_name.size())
        , exe_name.data()
        , k_flags_start);

    fprintf(stdout, "\nGeneral <flags>:\n");
    for (const auto& arg : k_args)
        fprintf(stdout, "\t%S:%S\n", arg.arg, arg.description);
    fprintf(stdout, "\nInput filters <flags>:\n");
    for (const auto& arg : k_notify_filters)
        fprintf(stdout, "\t%S:%S\n", arg.arg, arg.description);
    fprintf(stdout, "\nOutput actions:\n");
    for (const auto& arg : k_actions)
        fprintf(stdout, "\t%S:%S\n", arg.arg, arg.description);
    
    fprintf(stdout, "\n");
    fprintf(stdout, "Example:\n\n");
    Options options;
    options.directory = L"C:\\";
    options.watch_sub_tree = true;
    options.notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME;
    options.print_time = false;
    LogOptions(exe_path, options);
    fprintf(stdout, "\n");

    const DirectoryChange changes[] =
    {
        {FILE_ACTION_ADDED,             L"new_file.txt"},
        {FILE_ACTION_RENAMED_OLD_NAME,  L"file_with_old_name.txt"},
        {FILE_ACTION_REMOVED,           L"file_was_removed.txt"},
        {FILE_ACTION_RENAMED_NEW_NAME,  L"file_with_new_name.txt"},
    };

    fprintf(stdout, "Possible output:\n\n");
    for (const DirectoryChange& change : changes)
        LogDirectoryChange(options, change, "");

    fprintf(stdout, "\n");
    return -2;
}

#if (__clang__)
#pragma clang diagnostic push
// Missing prototype for wmain()
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

int wmain(int argc, wchar_t* argv[])
{
    std::ios::sync_with_stdio(false);
    
    Options options;
    const std::wstring_view exe_path = argv[0];
    if (!ParseCommandLine(argc, argv, options))
        return PrettyPrintHelp(exe_path);
    if (options.verbose)
        LogOptions(exe_path, options);

    return WatchForever(options);
}

#if (__clang__)
#pragma clang diagnostic pop
#endif
