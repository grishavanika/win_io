#include <win_io/detail/read_directory_changes.h>

#include <date/date.h>

#include <string>
#include <string_view>
#include <iostream>
#include <exception>

#include <cstdio>

#include <Windows.h>

using namespace wi;

using PrintChangesError = std::runtime_error;

struct Options
{
    bool watch_sub_tree = false;
    DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME;
    const wchar_t* directory = L".";
    bool verbose = false;
    bool print_help = false;
    bool print_time = false;
};

struct FlagMeta
{
    const DWORD flag;
    const wchar_t* const arg;
    const wchar_t* const description;
};

const FlagMeta k_notify_filters[] =
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

const FlagMeta k_actions[] =
{
    {FILE_ACTION_ADDED,             L"a", L"File was added to the directory"},
    {FILE_ACTION_REMOVED,           L"r", L"File was removed from the directory"},
    {FILE_ACTION_MODIFIED,          L"m", L"File was modified (time stamp or attributes)"},
    {FILE_ACTION_RENAMED_OLD_NAME,  L"o", L"File was renamed and this is the old name"},
    {FILE_ACTION_RENAMED_NEW_NAME,  L"n", L"File was renamed and this is the new name"},
};

static const wchar_t k_flags_start     = L'+';
static const wchar_t k_opt_recursive[] = L"r";
static const wchar_t k_opt_help[]      = L"h";
static const wchar_t k_opt_verbose[]   = L"v";

const struct
{
    const wchar_t* const arg;
    const wchar_t* const description;
} k_args[] =
{
    {k_opt_recursive, L"Recursive directory watch (watch with subtree)"},
    {k_opt_verbose,   L"Verbose output to stderr (errors + options)"},
    {k_opt_help,      L"Print help and exit"},
};

static std::wstring_view GetActionArg(DWORD action_id)
{
    for (const auto& action : k_actions)
    {
        if (action.flag == action_id)
        {
            return action.arg;
        }
    }

    throw PrintChangesError("Unknown action: " + std::to_string(action_id));
}

static DWORD GetFilterFromString(std::wstring_view str)
{
    for (const auto& filter : k_notify_filters)
    {
        // std::string_view operator==() for const char* is missing
        if (str.compare(filter.arg) == 0)
        {
            return filter.flag;
        }
    }

    throw PrintChangesError("Invalid filter string");
}

static std::wstring_view GetFileNameOnly(std::wstring_view exe_path)
{
    const auto pos = exe_path.find_last_of(L"\\/");
    if (pos == exe_path.npos)
    {
        return exe_path;
    }
    return exe_path.substr(pos + 1);
}

static void PrettyPrintDirectoryChange(
      const Options& options
    , const DirectoryChange& change
    , std::chrono::system_clock::time_point time)
{
    using namespace date;

    if (options.print_time)
    {
        std::wcout << L"[" << time << L"] ";
    }

    std::wcout << k_flags_start
        << GetActionArg(change.action)
        << L" " << change.name << L"\n";
}

static void PrettyPrintOptions(std::wstring_view exe_path, const Options& options)
{
    std::wcout << L"  " << GetFileNameOnly(exe_path) << L" ";
    std::wcout << options.directory << L" ";

    std::wcout << k_flags_start;
    if (options.print_help)
    {
        std::wcout << k_opt_help;
    }
    if (options.verbose)
    {
        std::wcout << k_opt_verbose;
    }
    if (options.watch_sub_tree)
    {
        std::wcout << k_opt_recursive;
    }
    for (const auto& filter : k_notify_filters)
    {
        if ((options.notify_filter & filter.flag) == filter.flag)
        {
            std::wcout << filter.arg;
        }
    }
    std::wcout << L"\n";
}

static void PrettyPrintHelp(std::wstring_view exe_path)
{
    std::wcout << L"Help:" << "\n";
    std::wcout << "\n";
    std::wcout << L"  " << GetFileNameOnly(exe_path) << L" ";
    std::wcout << L"<directory>" << L" ";
    std::wcout << k_flags_start << L"flags" << " " << "[time]" << L"\n";
    std::wcout << "\n";
    std::wcout << L"Note: " << k_flags_start
        << " without any flags (or empty filters flags)"
        << " enables all filters flags." << L"\n";
    std::wcout << L"Note: " << "'time' arg (print change time) is optional." << L"\n";

    std::wcout << L"\n";
    std::wcout << L"General flags:" << L"\n";
    for (const auto& arg : k_args)
    {
        std::wcout << L"\t" << arg.arg << L": " << arg.description << L"\n";
    }

    std::wcout << L"\n";
    std::wcout << L"Input filters flags:" << L"\n";
    for (const auto& filter : k_notify_filters)
    {
        std::wcout << L"\t" << filter.arg << L": " << filter.description << L"\n";
    }

    std::wcout << L"\n";
    std::wcout << L"Output actions:" << L"\n";
    for (const auto& action : k_actions)
    {
        std::wcout << L"\t" << action.arg << L": " << action.description << L"\n";
    }

    std::wcout << L"\n";
    std::wcout << L"Example:" << L"\n";
    Options options;
    options.directory = L"C:\\";
    options.watch_sub_tree = true;
    options.notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME;
    options.print_time = false;
    PrettyPrintOptions(exe_path, options);

    std::wcout << L"\n";
    std::wcout << L"Possible output:" << L"\n";
    const DirectoryChange changes[] =
    {
        {FILE_ACTION_ADDED,             L"new_file.txt"},
        {FILE_ACTION_RENAMED_OLD_NAME,  L"file_with_old_name.txt"},
        {FILE_ACTION_REMOVED,           L"file_was_removed.txt"},
        {FILE_ACTION_RENAMED_NEW_NAME,  L"file_with_new_name.txt"},
    };

    const auto time = std::chrono::system_clock::now();
    for (const auto& change : changes)
    {
        PrettyPrintDirectoryChange(options, change, time);
    }

    std::wcout << L"\n";
}

static Options ParseOptions(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        throw PrintChangesError("Expecting 2+ arguments, not "
            + std::to_string(argc - 1));
    }
    Options options;
    options.directory = argv[1];
    const std::wstring_view flags = argv[2];
    if ((flags.size() < 1) || (flags.front() != k_flags_start))
    {
        // #TODO: convert wstring to string and add useful context to error message
        throw PrintChangesError("Invalid flags argument");
    }

    if (argc >= 4)
    {
        options.print_time = (wcscmp(argv[3], L"time") == 0);
    }

    bool enable_all_filters = true;
    for (auto filter_char : flags.substr(1))
    {
        const std::wstring_view filter_str(&filter_char, 1);
        if (filter_str.compare(k_opt_recursive) == 0)
        {
            options.watch_sub_tree = true;
        }
        else if (filter_str.compare(k_opt_verbose) == 0)
        {
            options.verbose = true;
        }
        else if (filter_str.compare(k_opt_help) == 0)
        {
            options.print_help = true;
        }
        else
        {
            options.notify_filter |= GetFilterFromString(filter_str);
            enable_all_filters = false;
        }
    }

    if (enable_all_filters)
    {
        for (const auto& filter : k_notify_filters)
        {
            options.notify_filter |= filter.flag;
        }
    }

    return options;
}

static void PrintChanges(const Options& options
    , const DirectoryChangesRange& range
    , std::chrono::system_clock::time_point time)
{
    for (auto change : range)
    {
        PrettyPrintDirectoryChange(options, change, time);
    }
    std::wcout.flush();
}

void HandleError(const Options& options, const char* message
    , const std::error_code& ec = std::error_code())
{
    if (options.verbose)
    {
        if (ec)
        {
            std::cerr << message << ": " << ec.message() << "\n";
        }
        else
        {
            std::cerr << message << "\n";
        }
    }
}

static void HandleNonDirectoryResults(const Options& options, const DirectoryChangesResults& results)
{
    if (!options.verbose)
    {
        return;
    }

    if (auto port_changes = results.port_changes())
    {
        std::cerr << "Unexpected I/O port event" << "\n";
        (void)port_changes;
    }
    else if (!results.directory_changes())
    {
        std::cerr << "Unexpected get without any changes" << "\n";
    }

    throw PrintChangesError("Unreachable code");
}

static void WatchForever(const Options& options)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(1, ec);
    if (!port)
    {
        HandleError(options, "Failed to create IoCompletionPort", ec);
        return;
    }

    DWORD buffer[32 * 1024];
    auto dir_changes = DirectoryChanges::make(
        options.directory
        , buffer
        , sizeof(buffer)
        , options.watch_sub_tree
        , options.notify_filter
        , *port
        , 1/*dir key*/
        , ec);
    if (!dir_changes)
    {
        HandleError(options, "Failed to create DirectoryChanges", ec);
        return;
    }

    while (true)
    {
        dir_changes->start_watch(ec);
        if (ec)
        {
            HandleError(options, "Failed to start watching", ec);
            continue;
        }

        const DirectoryChangesResults results = dir_changes->get(ec);
        if (ec)
        {
            HandleError(options, "Failed to get changes", ec);
            continue;
        }

        auto time = std::chrono::system_clock::now();

        auto changes = results.directory_changes();
        if (!changes)
        {
            HandleNonDirectoryResults(options, results);
            continue;
        }

        PrintChanges(options, *changes, time);
    }
}


#if (__clang__)
#pragma clang diagnostic push
// Missing prototype for wmain()
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

int wmain(int argc, wchar_t* argv[])
{
    // C streams are not used, no need to synchronize
    std::ios::sync_with_stdio(false);

    std::wstring_view exe_path = argv[0];
    if (argc == 1)
    {
        PrettyPrintHelp(exe_path);
        return EXIT_FAILURE;
    }

    Options options;
    try
    {
        options = ParseOptions(argc, argv);
    }
    catch (const PrintChangesError& e)
    {
        std::cerr << "Failed to parse command line: " << e.what() << "\n";
        PrettyPrintHelp(exe_path);
        return EXIT_FAILURE;
    }

    if (options.print_help)
    {
        PrettyPrintOptions(exe_path, options);
        PrettyPrintHelp(exe_path);
        return EXIT_FAILURE;
    }

    if (options.verbose)
    {
        PrettyPrintOptions(exe_path, options);
    }

    WatchForever(options);
}

#if (__clang__)
#pragma clang diagnostic pop
#endif
