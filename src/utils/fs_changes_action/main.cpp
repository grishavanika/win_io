#include <win_io/detail/read_directory_changes.h>
#include <win_io/detail/io_completion_port.h>
#include <win_io/detail/read_directory_changes.h>

#include <string>
#include <rxcpp/rx.hpp>

#include <Windows.h>

namespace io = wi::detail;

namespace rx = rxcpp;
namespace rxu = rxcpp::util;
namespace rxs = rxcpp::subjects;
namespace rxo = rxcpp::operators;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	{ // Temporary make std::cout available
		::AllocConsole();
		FILE* unused;
		freopen_s(&unused, "CONOUT$", "w", stdout);
	}

	io::IoCompletionPort io_service(1/*threads count*/);
	rxs::subject<io::PortData> io_port_data;
	rx::observable<io::PortData> io_changes = io_port_data.get_observable();
	auto io_port = io_port_data.get_subscriber();
	
	const io::WinDWORD k_filters = 0
		| FILE_NOTIFY_CHANGE_FILE_NAME
		| FILE_NOTIFY_CHANGE_DIR_NAME
		| FILE_NOTIFY_CHANGE_ATTRIBUTES
		| FILE_NOTIFY_CHANGE_SIZE
		| FILE_NOTIFY_CHANGE_LAST_WRITE
		| FILE_NOTIFY_CHANGE_LAST_ACCESS
		| FILE_NOTIFY_CHANGE_CREATION
		| FILE_NOTIFY_CHANGE_SECURITY;

	DWORD buffer[16 * 1024];
	io::DirectoryChanges dir_changes(L"C:\\", buffer, sizeof(buffer)
		, true/*watch_sub_tree*/, k_filters, io_service);

	auto pipeline = io_changes
		| rxo::transform([&](io::PortData pd)
		{
			return io::DirectoryChangesRange(buffer, pd);
		})
		| rxo::tap([&](io::DirectoryChangesRange changes)
		{
			for (const auto change : changes)
			{
				std::wcout.write(change.name.data(), change.name.size());
				std::wcout << L"\n";
			}
			std::wcout.flush();

			// #XXX: should be as separate action
			dir_changes.start_watch();
		})
		| rxo::as_dynamic();

	// #XXX: how to provide subscriber ?
	// Convert cold to hot ?
	pipeline.subscribe([](io::DirectoryChangesRange) {});


	// Start watching
	dir_changes.start_watch();

	while (true)
	{
		std::error_code ec;
		auto data = io_service.get(ec);
		if (data)
		{
			io_port.on_next(std::move(*data));
		}
		else
		{
			// #XXX: errors should be pushed to pipeline
			assert(ec);
			std::cout << "ERROR: " << ec.message() << std::endl;
		}
	}

	return 0;
}
