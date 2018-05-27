#include <win_io/detail/read_directory_changes.h>

#include <string>
#include <string_view>
#include <iostream>
#include <exception>

#include <cstdio>

#include <Windows.h>

namespace
{

using namespace wi::detail;

using PrintChangesError = std::runtime_error;

struct Options
{
	bool watch_sub_tree = false;
	DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME;
	const wchar_t* directory = L".";
	bool verbose = false;
	bool print_help = false;
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

wchar_t GetFlagsStartChar()
{
	return L'+';
}

const wchar_t* GetRecursiveOption()
{
	return L"r";
}

const wchar_t* GetHelpOption()
{
	return L"h";
}

const wchar_t* GetVerboseOption()
{
	return L"v";
}

std::wostream& LogStreamW()
{
	return std::wcout;
}

std::ostream& ErrorStream()
{
	return (std::cerr << "[E] ");
}

const struct
{
	const wchar_t* const arg;
	const wchar_t* const description;
} k_args[] =
{
	{GetRecursiveOption(), L"Recursive directory watch (watch with subtree)"},
	{GetVerboseOption(),   L"Verbose output to stderr (errors + options)"},
	{GetHelpOption(),      L"Print help and exit"},
};

std::wstring_view GetActionArg(DWORD action_id)
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

DWORD GetFilterFromString(std::wstring_view str)
{
	for (const auto& filter : k_notify_filters)
	{
		if (str == filter.arg)
		{
			return filter.flag;
		}
	}

	throw PrintChangesError("Invalid filter string");
}

std::wstring_view GetFileNameOnly(std::wstring_view exe_path)
{
	const auto pos = exe_path.find_last_of(L"\\/");
	if (pos == exe_path.npos)
	{
		return exe_path;
	}
	return exe_path.substr(pos + 1);
}

void PrettyPrintDirectoryChange(const DirectoryChange& change)
{
	LogStreamW() << GetFlagsStartChar() << GetActionArg(change.action)
		<< L" " << change.name << L"\n";
}

void PrettyPrintOptions(std::wstring_view exe_path, const Options& options)
{
	LogStreamW() << GetFileNameOnly(exe_path) << L" ";
	LogStreamW() << options.directory << L" ";

	LogStreamW() << GetFlagsStartChar();
	if (options.print_help)
	{
		LogStreamW() << GetHelpOption();
	}
	if (options.verbose)
	{
		LogStreamW() << GetVerboseOption();
	}
	if (options.watch_sub_tree)
	{
		LogStreamW() << GetRecursiveOption();
	}
	for (const auto& filter : k_notify_filters)
	{
		if ((options.notify_filter & filter.flag) == filter.flag)
		{
			LogStreamW() << filter.arg;
		}
	}
	LogStreamW() << L"\n";
}

void PrettyPrintHelp(std::wstring_view exe_path)
{
	LogStreamW() << L"Help:" << "\n";
	LogStreamW() << GetFileNameOnly(exe_path) << L" ";
	LogStreamW() << L"<directory>" << L" ";
	LogStreamW() << GetFlagsStartChar() << L"flags" << L"\n";
	LogStreamW() << L"Note: " << GetFlagsStartChar()
		<< " without any flags (or empty filters flags)"
		<< " means all filters flags" << L"\n";

	LogStreamW() << L"General flags:" << L"\n";
	for (const auto& arg : k_args)
	{
		LogStreamW() << L"\t" << arg.arg << L": " << arg.description << L"\n";
	}

	LogStreamW() << L"Input filters flags:" << L"\n";
	for (const auto& filter : k_notify_filters)
	{
		LogStreamW() << L"\t" << filter.arg << L": " << filter.description << L"\n";
	}

	LogStreamW() << L"Output actions flags:" << L"\n";
	for (const auto& action : k_actions)
	{
		LogStreamW() << L"\t" << action.arg << L": " << action.description << L"\n";
	}

	LogStreamW() << L"Example:" << L"\n";
	Options options;
	options.directory = L"C:\\";
	options.watch_sub_tree = true;
	options.notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME;
	PrettyPrintOptions(exe_path, options);

	LogStreamW() << L"\n";
	LogStreamW() << L"Possible output:" << L"\n";
	const DirectoryChange changes[] =
	{
		{FILE_ACTION_ADDED,             L"new_file.txt"},
		{FILE_ACTION_RENAMED_OLD_NAME,  L"file_with_old_name.txt"},
		{FILE_ACTION_REMOVED,           L"file_was_removed.txt"},
		{FILE_ACTION_RENAMED_NEW_NAME,  L"file_with_new_name.txt"},
	};

	for (const auto& change : changes)
	{
		PrettyPrintDirectoryChange(change);
	}

	LogStreamW() << L"\n";
}

Options ParseOptions(int argc, wchar_t* argv[])
{
	if (argc != 3)
	{
		throw PrintChangesError("Expecting 2 arguments, not "
			+ std::to_string(argc - 1));
	}
	Options options;
	options.directory = argv[1];
	const std::wstring_view flags = argv[2];
	if ((flags.size() < 1) || (flags.front() != GetFlagsStartChar()))
	{
		// #TODO: convert wstring to string and add useful context to error message
		throw PrintChangesError("Invalid flags argument");
	}

	bool enable_all_filters = true;
	for (auto filter_char : flags.substr(1))
	{
		const std::wstring_view filter_str(&filter_char, 1);
		if (filter_str == GetRecursiveOption())
		{
			options.watch_sub_tree = true;
		}
		else if (filter_str == GetVerboseOption())
		{
			options.verbose = true;
		}
		else if (filter_str == GetHelpOption())
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

void PrintChanges(const DirectoryChangesRange& range)
{
	for (auto change : range)
	{
		PrettyPrintDirectoryChange(change);
	}
}

void HandleError(const Options& options, const char* message, const std::error_code& ec)
{
	if (options.verbose)
	{
		ErrorStream() << message << ": " << ec.message() << "\n";
	}
}

void HandleNonDirectoryResults(const Options& options, const DirectoryChangesResults& results)
{
	if (!options.verbose)
	{
		return;
	}

	if (auto port_changes = results.port_changes())
	{
		ErrorStream() << "Unexpected I/O port event" << "\n";
		(void)port_changes;
	}
	else if (!results.has_changes())
	{
		ErrorStream() << "Unexpected get without any changes" << "\n";
	}

	throw PrintChangesError("Unreachable code");
}


[[noreturn]] void WatchForever(const Options& options)
{
	DWORD buffer[16 * 1024];
	IoCompletionPort port;
	DirectoryChanges dir_changes(options.directory, buffer, sizeof(buffer)
		, options.watch_sub_tree, options.notify_filter, port);

	while (true)
	{
		std::error_code ec;
		dir_changes.start_watch(ec);
		if (ec)
		{
			HandleError(options, "Failed to start watching", ec);
			continue;
		}

		const DirectoryChangesResults results = dir_changes.get(ec);
		if (ec)
		{
			HandleError(options, "Failed to get changes", ec);
			continue;
		}

		if (auto changes = results.directory_changes())
		{
			PrintChanges(*changes);
			continue;
		}

		HandleNonDirectoryResults(options, results);
	}
}

} // namespace

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
		ErrorStream() << "Failed to parse command line: " << e.what() << "\n";
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

	try
	{
		WatchForever(options);
	}
	catch (const std::exception& e)
	{
		ErrorStream() << "Fatal error: " << e.what() << "\n";
		return EXIT_FAILURE;
	}
}

#if (__clang__)
#pragma clang diagnostic pop
#endif
