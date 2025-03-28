#include <win_io/read_directory_changes.h>
#include <win_io/io_completion_port.h>

#include <rxcpp/rx.hpp>

#include <sstream>
#include <string>

#include <Windows.h>

namespace io = wi;

namespace rx = rxcpp;
namespace rxu = rxcpp::util;
namespace rxs = rxcpp::subjects;
namespace rxo = rxcpp::operators;

struct DirectoryEvent
{
    io::DirectoryChanges* dir_watcher = nullptr;
    io::DirectoryChangesRange changes;
};

struct DirectoryChange
{
    io::DirectoryChanges* dir_watcher = nullptr;
    io::DirectoryChange data;
};

struct UIModel
{
    std::wstring page;
};

struct ActionMeta
{
    const DWORD action;
    const wchar_t* const title;
};

const ActionMeta k_actions[] =
{
    {FILE_ACTION_ADDED,             L"a"},
    {FILE_ACTION_REMOVED,           L"r"},
    {FILE_ACTION_MODIFIED,          L"m"},
    {FILE_ACTION_RENAMED_OLD_NAME,  L"o"},
    {FILE_ACTION_RENAMED_NEW_NAME,  L"n"},
};

std::wstring_view GetActionTitle(DWORD action_id)
{
    for (const auto& action : k_actions)
    {
        if (action.action == action_id)
        {
            return action.title;
        }
    }
    assert(false);
    return {};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    { // Temporary make std::cout available, ignore any errors
        (void)::AllocConsole();
        FILE* unused;
        (void)freopen_s(&unused, "CONOUT$", "w", stdout);
    }

    std::error_code ec;
    auto io_service = io::IoCompletionPort::make(1/*threads count*/, ec);
    assert(!ec);
    rxs::subject<io::PortEntry> io_port_data;
    rx::observable<io::PortEntry> io_changes = io_port_data.get_observable();
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

    DWORD buffer_c[16 * 1024];
    auto dir_watcher_c = io::DirectoryChanges::make(L"C:\\", buffer_c, sizeof(buffer_c)
        , true/*watch sub tree*/, k_filters, *io_service, 1/*dir_key*/, ec);
    assert(!ec);
    DWORD buffer_d[16 * 1024];
    auto dir_watcher_d = io::DirectoryChanges::make(L"D:\\", buffer_d, sizeof(buffer_d)
        , true/*watch sub tree*/, k_filters, *io_service, 2/*dir_key*/, ec);
    assert(!ec);

    io::DirectoryChanges* dirs[] = { &*dir_watcher_c, &*dir_watcher_d };

    auto dir_events = io_changes
        .group_by([&](io::PortEntry pd)
            {
                for (io::DirectoryChanges* dir : dirs)
                {
                    if (dir->is_valid_directory_change(pd))
                    {
                        return dir;
                    }
                }
                return static_cast<io::DirectoryChanges*>(nullptr);
            })
        .filter([](rx::grouped_observable<io::DirectoryChanges*, io::PortEntry> group)
            {
                io::DirectoryChanges* dir = group.get_key();
                return (dir != nullptr);
            })
                .merge_transform([](rx::grouped_observable<io::DirectoryChanges*, io::PortEntry> group)
                    {
                        io::DirectoryChanges* dir = group.get_key();
                        return group.transform([dir](io::PortEntry pd)
                            {
                                return DirectoryEvent{ dir, io::DirectoryChangesRange(dir->buffer(), pd) };
                            });
                    });

            auto dir_changes = dir_events
                .merge_transform([](DirectoryEvent event)
                    {
                        return rx::observable<>::iterate(event.changes)
                            .transform([dir_watcher = event.dir_watcher]
                            (io::DirectoryChange change)
                                {
                                    return DirectoryChange{ dir_watcher, change };
                                });
                    });

            auto collect_ui = dir_changes
                .transform([](DirectoryChange change)
                    {
                        std::wstringstream data;
                        data << L"+" << GetActionTitle(change.data.action) << ": ";
                        data.write(change.data.name.data(), change.data.name.size());
                        data << L"\n";
                        return UIModel{ data.str() };
                    });

            collect_ui.subscribe([](UIModel data)
                {
                    std::wcout << data.page;
                });

            dir_events.subscribe([](DirectoryEvent event)
                {
                    // Flush pending UI change.
                    // (Singe event may produce multiple UI changes).
                    std::wcout << std::wstring(20, L'-') << std::endl;

                    // Listen to new changes.
                    // #XXX: errors should be pushed to pipeline
                    std::error_code ec;
                    event.dir_watcher->start_watch(ec);
                    assert(ec);
                });

            /////////////////////////////////////////////
            // Start watching initial changes
            for (io::DirectoryChanges* dir : dirs)
            {
                // #XXX: errors should be pushed to pipeline
                dir->start_watch(ec);
                assert(!ec);
            }

            while (true)
            {
                auto data = io_service->get(ec);
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
