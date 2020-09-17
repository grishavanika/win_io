#include <win_io/detail/read_directory_changes.h>
#include <win_io/detail/last_error_utils.h>

#include <chrono>

#include <cassert>

#include <Windows.h>

using namespace wi;

namespace
{
    OVERLAPPED& GetOverlapped(WinOVERLAPPED& ov)
    {
        return *reinterpret_cast<OVERLAPPED*>(&ov);
    }
} // namespace

/*static*/ std::optional<DirectoryChanges> DirectoryChanges::make(
    WinHANDLE directory, void* buffer
    , WinDWORD length, bool watch_sub_tree, WinDWORD notify_filter
    , IoCompletionPort& io_port, WinULONG_PTR dir_key
    , std::error_code& ec) noexcept
{
    assert((reinterpret_cast<std::uintptr_t>(buffer) % sizeof(WinDWORD) == 0)
        && "[Dc] Buffer should be DWORD aligned");
    assert(((directory != INVALID_HANDLE_VALUE) && (directory != nullptr))
        && "[Dc] Directory should be valid");

    io_port.associate_device(directory, dir_key, ec);
    if (ec)
    {
        return std::nullopt;
    }

    std::optional<DirectoryChanges> value;
    DirectoryChanges& o = value.emplace();
    o.io_port_ = &io_port;
    o.directory_ = directory;
    o.buffer_ = buffer;
    o.dir_key_ = dir_key;
    o.ov_ = {};
    o.length_ = length;
    o.notify_filter_ = notify_filter;
    o.owns_directory_ = false;
    o.watch_sub_tree_ = watch_sub_tree;
    return value;
}

/*static*/ std::optional<DirectoryChanges> DirectoryChanges::make(
    const wchar_t* directory_name
    , void* buffer, WinDWORD length
    , bool watch_sub_tree, WinDWORD notify_filter
    , IoCompletionPort& io_port, WinULONG_PTR dir_key
    , std::error_code& ec) noexcept
{
    WinHANDLE directory = open_directory(directory_name, ec);
    if (ec)
    {
        return std::nullopt;
    }

    if (auto o = make(directory, buffer, length, watch_sub_tree, notify_filter, io_port, dir_key, ec);
        o.has_value() && !ec)
    {
        o->owns_directory_ = true;
        return o;
    }

    (void)::CloseHandle(directory);
    return std::nullopt;
}

DirectoryChanges::DirectoryChanges(DirectoryChanges&& rhs) noexcept
    : io_port_(std::exchange(rhs.io_port_, nullptr))
    , directory_(std::exchange(rhs.directory_, INVALID_HANDLE_VALUE))
    , buffer_(std::exchange(rhs.buffer_, nullptr))
    , dir_key_(std::exchange(rhs.dir_key_, 0))
    , ov_{}
    , length_(std::exchange(rhs.length_, 0))
    , notify_filter_(std::exchange(rhs.notify_filter_, 0))
    , owns_directory_(std::exchange(rhs.owns_directory_, false))
    , watch_sub_tree_(std::exchange(rhs.watch_sub_tree_, false))
{
    ov_ = rhs.ov_;
}

DirectoryChanges& DirectoryChanges::operator=(DirectoryChanges&& rhs) noexcept
{
    if (this == &rhs)
    {
        return *this;
    }
    close();

    io_port_ = std::exchange(rhs.io_port_, nullptr);
    directory_ = std::exchange(rhs.directory_, INVALID_HANDLE_VALUE);
    buffer_ = std::exchange(rhs.buffer_, nullptr);
    dir_key_ = std::exchange(rhs.dir_key_, 0);
    ov_ = rhs.ov_;
    length_ = std::exchange(rhs.length_, 0);
    notify_filter_ = std::exchange(rhs.notify_filter_, 0);
    owns_directory_ = std::exchange(rhs.owns_directory_, false);
    watch_sub_tree_ = std::exchange(rhs.watch_sub_tree_, false);
    return *this;
}

/*static*/ WinHANDLE DirectoryChanges::open_directory(const wchar_t* directory_name, std::error_code& ec)
{
    const auto handle = ::CreateFileW(directory_name
        , FILE_LIST_DIRECTORY
        // Anyone can do whatever needed.
        , FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE
        , nullptr
        , OPEN_EXISTING
        // Open as directory, async.
        , FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
        , nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        ec = detail::make_last_error_code();
    }
    return handle;
}

WinHANDLE DirectoryChanges::directory_handle() const
{
    return directory_;
}

const void* DirectoryChanges::buffer() const
{
    return buffer_;
}

DirectoryChanges::~DirectoryChanges()
{
    close();
}

void DirectoryChanges::close() noexcept
{
    if (owns_directory_)
    {
        (void)::CloseHandle(directory_);
        directory_ = nullptr;
        owns_directory_ = false;
    }
}

void DirectoryChanges::start_watch(std::error_code& ec)
{
    ec = std::error_code();
    // See ::ReadDirectoryChangesExW(): starting from Windows 10.
    const BOOL status = ::ReadDirectoryChangesW(directory_
        , buffer_, length_, watch_sub_tree_, notify_filter_
        , nullptr, &GetOverlapped(ov_), nullptr);
    if (!status)
    {
        ec = detail::make_last_error_code();
    }
}

bool DirectoryChanges::is_directory_change(const PortData& data) const
{
    return (data.key == dir_key_) && (data.ptr == &ov_);
}

bool DirectoryChanges::has_buffer_overflow(const PortData& data) const
{
    const auto buffer_size = data.value;
    return is_directory_change(data)
        && (buffer_size == 0);
}

bool DirectoryChanges::is_valid_directory_change(const PortData& data) const
{
    const auto buffer_size = data.value;
    return is_directory_change(data)
        && (buffer_size != 0);
}

DirectoryChangesResults DirectoryChanges::wait_impl(
    WinDWORD milliseconds, std::error_code& ec)
{
    auto data = io_port_->wait_for(std::chrono::milliseconds(milliseconds), ec);
    if (!data)
    {
        return DirectoryChangesResults();
    }
    // Check if data is coming from our directory, since we do not
    // own I/O Completion Port and it can be used for other purpose.
    // Also we check whether there is actual data (i.e, no buffer overflow)
    if (!is_valid_directory_change(*data))
    {
        return DirectoryChangesResults(std::move(*data));
    }
    return DirectoryChangesResults(DirectoryChangesRange(buffer_, std::move(*data)));
}

DirectoryChangesResults DirectoryChanges::get(std::error_code& ec)
{
    return wait_impl(INFINITE, ec);
}

DirectoryChangesResults DirectoryChanges::query(std::error_code& ec)
{
    return wait_impl(0, ec);
}
