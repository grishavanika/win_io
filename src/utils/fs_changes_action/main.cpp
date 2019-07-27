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
	{ // Temporary make std::cout available, ignore any errors
		(void)::AllocConsole();
		FILE* unused;
		(void)freopen_s(&unused, "CONOUT$", "w", stdout);
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
	io::DirectoryChanges dir_watcher(L"C:\\", buffer, sizeof(buffer)
		, true/*watch_sub_tree*/, k_filters, io_service);

	using namespace rxo;

	auto dir_changes = io_changes
		.filter([&](io::PortData pd)
		{
			return dir_watcher.is_valid_directory_change(pd);
		})
		.map([&](io::PortData pd)
		{
			return rx::observable<>::just(
				io::DirectoryChangesRange(buffer, pd));
		});
	
	auto dir_notifications = dir_changes
		.merge_transform([](rx::observable<io::DirectoryChangesRange> changes)
		{
			return changes.merge_transform([](io::DirectoryChangesRange range)
			{
				return rx::observable<>::iterate(range);
			});
		});

	struct noop {};

	auto draw_ui = dir_notifications
		.tap([](io::DirectoryChange change)
		{
			std::wcout.write(change.name.data(), change.name.size());
			std::wcout << L"\n";
		})
		.transform([](io::DirectoryChange)
			{ return noop(); });

	auto flush_ui = dir_changes
		// #XXX: how to ignore parameter ?
		.tap([&](rx::observable<io::DirectoryChangesRange>)
		{
			std::wcout.flush();
		})
		.transform([](rx::observable<io::DirectoryChangesRange>)
			{ return noop(); });

	auto repeat_watch = dir_changes
		.tap([&](rx::observable<io::DirectoryChangesRange>)
		{
			dir_watcher.start_watch();
		})
		.transform([](rx::observable<io::DirectoryChangesRange>)
			{ return noop(); });

	draw_ui
		.merge(flush_ui)
		.merge(repeat_watch)
		.subscribe([](noop) {});

	/////////////////////////////////////////////
	dir_watcher.start_watch();

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
