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

struct FlagArg
{
	const DWORD flag;
	const wchar_t* const arg;
	const wchar_t* const description;
};

const FlagArg k_notify_filters[] =
{
	{FILE_NOTIFY_CHANGE_FILE_NAME,		L"f"
		, L"File name change (renaming, creating, or deleting a file)"},
	{FILE_NOTIFY_CHANGE_DIR_NAME,		L"d"
		, L"Directory name change (creating or deleting a directory)"},
	{FILE_NOTIFY_CHANGE_ATTRIBUTES,		L"a"
		, L"Attribute change"},
	{FILE_NOTIFY_CHANGE_SIZE,			L"s"
		, L"File size change"},
	{FILE_NOTIFY_CHANGE_LAST_WRITE,		L"w"
		, L"Last write time change of files"},
	{FILE_NOTIFY_CHANGE_LAST_ACCESS,	L"t"
		, L"Last access time change of files"},
	{FILE_NOTIFY_CHANGE_CREATION,		L"c"
		, L"Creation time change of files"},
	{FILE_NOTIFY_CHANGE_SECURITY,		L"s"
		, L"Security descriptor change"},
};

const FlagArg k_actions[] =
{
	{FILE_ACTION_ADDED,				L"a"
		, L"File was added to the directory"},
	{FILE_ACTION_REMOVED,			L"r"
		, L"File was removed from the directory"},
	{FILE_ACTION_MODIFIED,			L"m"
		, L"File was modified (time stamp or attributes)"},
	{FILE_ACTION_RENAMED_OLD_NAME,	L"o"
		, L"File was renamed and this is the old name"},
	{FILE_ACTION_RENAMED_NEW_NAME,	L"n"
		, L"File was renamed and this is the new name"},
};

// #TODO: detecting these args is hard-coded in the code in few places
const struct
{
	const wchar_t* const arg;
	const wchar_t* const description;
} k_args[] =
{
	{L"r", L"Recursive directory watch (watch with subtree)"},
	{L"v", L"Verbose output (errors + options)"},
	{L"h", L"Print help and exit"},
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

const DWORD* GetFilterFromString(std::wstring_view str)
{
	for (const auto& filter : k_notify_filters)
	{
		if (str == filter.arg)
		{
			return &filter.flag;
		}
	}

	return nullptr;
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

std::wostream& PrintQuotedIfSpaces(std::wostream& o, std::wstring_view path)
{
	if (path.find(L' ') != path.npos)
	{
		o << L"\"" << path << L"\"";
	}
	else
	{
		o << path;
	}
	return o;
}

std::wostream& PrettyPrintDirectoryChange(std::wostream& o, const DirectoryChange& change)
{
	o << L"+" << GetActionArg(change.action) << L" " << change.name;
	return o;
}

void PrettyPrintOptions(std::wstring_view exe_path, const Options& options)
{
	std::wcout << GetFileNameOnly(exe_path) << L" ";
	PrintQuotedIfSpaces(std::wcout, options.directory) << L" ";

	std::wcout << L"+";
	if (options.print_help)
	{
		std::wcout << L"h";
	}
	if (options.verbose)
	{
		std::wcout << L"v";
	}
	if (options.watch_sub_tree)
	{
		std::wcout << L"r";
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

void PrettyPrintHelp(std::wstring_view exe_path)
{
	std::wcout << L"Help:" << "\n";
	std::wcout << GetFileNameOnly(exe_path) << L" ";
	std::wcout << L"<directory>" << L" ";
	std::wcout << L"+flags" << L"\n";

	std::wcout << L"General args:" << L"\n";
	for (const auto& arg : k_args)
	{
		std::wcout << L"\t" << arg.arg << L": " << arg.description << L"\n";
	}

	std::wcout << L"Input filters:" << L"\n";
	for (const auto& filter : k_notify_filters)
	{
		std::wcout << L"\t" << filter.arg << L": " << filter.description << L"\n";
	}

	std::wcout << L"Output actions:" << L"\n";
	for (const auto& action : k_actions)
	{
		std::wcout << L"\t" << action.arg << L": " << action.description << L"\n";
	}

	std::wcout << L"Example:" << L"\n";
	Options options;
	options.directory = L"C:\\";
	options.watch_sub_tree = true;
	options.notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME;
	PrettyPrintOptions(exe_path, options);

	std::wcout << L"Possible output:" << L"\n";
	const DirectoryChange changes[] =
	{
		{FILE_ACTION_ADDED, L"new_file.txt"},
		{FILE_ACTION_RENAMED_OLD_NAME, L"file_with_old_name.txt"},
		{FILE_ACTION_REMOVED, L"file_was_removed.txt"},
		{FILE_ACTION_RENAMED_NEW_NAME, L"file_with_new_name.txt"},
	};

	for (const auto& change : changes)
	{
		PrettyPrintDirectoryChange(std::wcout, change) << L"\n";
	}

	std::wcout << L"\n";
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
	if ((flags.size() < 1) || (flags.front() != L'+'))
	{
		// #TODO: convert wstring to string and add useful context to error message
		throw PrintChangesError("Invalid flags argument");
	}

	bool enable_all_filters = true;
	for (auto filter_arg : flags.substr(1))
	{
		if (filter_arg == L'r')
		{
			options.watch_sub_tree = true;
			continue;
		}
		if (filter_arg == L'v')
		{
			options.verbose = true;
			continue;
		}
		if (filter_arg == L'h')
		{
			options.print_help = true;
			continue;
		}

		const DWORD* filter = GetFilterFromString(std::wstring_view(&filter_arg, 1));
		if (!filter)
		{
			throw PrintChangesError("Invalid flag was specified");
		}
		enable_all_filters = false;
		options.notify_filter |= *filter;
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
		PrettyPrintDirectoryChange(std::wcout, change);
		std::wcout << L"\n";
	}
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
		if (ec && options.verbose)
		{
			std::cerr << "[E] Failed to start watching: " << ec.message() << "\n";
			continue;
		}
		else if (ec)
		{
			continue;
		}

		auto results = dir_changes.get(ec);
		if (auto changes = results.directory_changes())
		{
			PrintChanges(*changes);
			continue;
		}
		if (!options.verbose)
		{
			continue;
		}
		if (ec)
		{
			std::cerr << "[E] Failed to get changes: " << ec.message() << "\n";
			continue;
		}
		else if (auto port_changes = results.port_changes())
		{
			std::cerr << "[E] Unexpected I/O port event" << "\n";
			(void)port_changes;
			continue;
		}
		else if (!results.has_changes())
		{
			std::cerr << "[E] Unexpected get without any changes" << "\n";
			continue;
		}

		throw PrintChangesError("Unreachable code");
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
		std::cerr << "[E] Failed to parse command line: " << e.what() << "\n";
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
		std::cerr << "[E] Fatal error: " << e.what() << "\n";
		return EXIT_FAILURE;
	}
}

#if (__clang__)
#pragma clang diagnostic pop
#endif
