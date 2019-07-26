#include <win_io/detail/read_directory_changes.h>

#include <chrono>

#include <cassert>

#include <Windows.h>

using namespace wi;
using namespace detail;

namespace
{
	OVERLAPPED& GetOverlapped(WinOVERLAPPED& ov)
	{
		return *reinterpret_cast<OVERLAPPED*>(&ov);
	}
} // namespace

DirectoryChanges::DirectoryChanges(WinHANDLE directory, void* buffer
	, WinDWORD length, bool watch_sub_tree, WinDWORD notify_filter
	, IoCompletionPort& io_port, WinULONG_PTR dir_key /*= 1*/)
	: io_port_(io_port)
	, directory_(directory)
	, buffer_(buffer)
	, dir_key_(dir_key)
	, ov_()
	, length_(length)
	, notify_filter_(notify_filter)
	, owns_directory_(false)
	, watch_sub_tree_(watch_sub_tree)
{
	assert((reinterpret_cast<std::uintptr_t>(buffer) % sizeof(WinDWORD) == 0)
		&& "[Dc] Buffer should be DWORD alligned");
	assert(((directory != INVALID_HANDLE_VALUE) && (directory != nullptr))
		&& "[Dc] Directory should be valid");

	// #TODO: throw DirectoryChangesError exception with nested
	// IoCompletionPortError if there is any
	io_port_.associate_device(directory, dir_key_);
}

DirectoryChanges::DirectoryChanges(const wchar_t* directory_name
	, void* buffer, WinDWORD length
	, bool watch_sub_tree, WinDWORD notify_filter
	, IoCompletionPort& io_port, WinULONG_PTR dir_key /*= 1*/)
	: DirectoryChanges(open_directory(directory_name)
		, buffer, length, watch_sub_tree, notify_filter, io_port, dir_key)
{
	owns_directory_ = true;
}

WinHANDLE DirectoryChanges::open_directory(const wchar_t* directory_name)
{
	const auto handle = ::CreateFileW(directory_name
		, FILE_LIST_DIRECTORY
		// Anyone can do whatever he wants
		, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE
		, nullptr
		, OPEN_EXISTING
		// Open as directory, async.
		, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
		, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		throw_last_error<DirectoryChangesError>("[Dc] ::CreateFileW()");
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
	if (owns_directory_)
	{
		::CloseHandle(directory_);
	}
}

void DirectoryChanges::start_watch(std::error_code& ec)
{
	ec = std::error_code();
	// See ::ReadDirectoryChangesExW(): starting from Windows 10
	const BOOL status = ::ReadDirectoryChangesW(directory_
		, buffer_, length_, watch_sub_tree_, notify_filter_
		, nullptr, &GetOverlapped(ov_), nullptr);
	if (!status)
	{
		ec = make_last_error_code();
	}
}

void DirectoryChanges::start_watch()
{
	std::error_code ec;
	start_watch(ec);
	throw_if_error<DirectoryChangesError>("[Dc] ::ReadDirectoryChangesW()", ec);
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
	const auto data = io_port_.wait_for(std::chrono::milliseconds(milliseconds), ec);
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
	return DirectoryChangesResults(
		DirectoryChangesRange(buffer_, std::move(*data)));
}

DirectoryChangesResults DirectoryChanges::get(std::error_code& ec)
{
	return wait_impl(INFINITE, ec);
}

DirectoryChangesResults DirectoryChanges::get()
{
	std::error_code ec;
	auto data = get(ec);
	throw_if_error<DirectoryChangesError>("[Dc] failed to wait for data", ec);
	return data;
}

DirectoryChangesResults DirectoryChanges::query(std::error_code& ec)
{
	return wait_impl(0, ec);
}

DirectoryChangesResults DirectoryChanges::query()
{
	std::error_code ec;
	auto data = query(ec);
	throw_if_error<DirectoryChangesError>("[Dc] failed to wait for data", ec);
	return data;
}
