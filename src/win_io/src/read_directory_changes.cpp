#include <win_io/detail/read_directory_changes.h>
#include <win_io/detail/read_directory_changes_errors.h>

#include <cassert>

#include <Windows.h>

using namespace wi;
using namespace detail;

namespace
{
	OVERLAPPED& GetOverlapped(WinOVERLAPPEDBuffer& buffer)
	{
		return *reinterpret_cast<OVERLAPPED*>(&buffer);
	}

	void CreateOverlapped(WinOVERLAPPEDBuffer& buffer)
	{
		new(static_cast<void*>(&buffer)) OVERLAPPED();
	}

	void DestroyOverlapped(WinOVERLAPPEDBuffer& buffer)
	{
		GetOverlapped(buffer).~OVERLAPPED();
	}
}

DirectoryChanges::DirectoryChanges(WinHANDLE directory, void* buffer
	, WinDWORD length, bool watch_sub_tree, WinDWORD notify_filter
	, IoCompletionPort& io_port, WinULONG_PTR dir_key /*= 1*/)
	: io_port_(io_port)
	, directory_(directory)
	, buffer_(buffer)
	, dir_key_(dir_key)
	, ov_buffer_()
	, length_(length)
	, notify_filter_(notify_filter)
	, owns_directory_(false)
	, watch_sub_tree_(watch_sub_tree)
{
	CreateOverlapped(ov_buffer_);

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

WinHANDLE DirectoryChanges::directory() const
{
	return directory_;
}

DirectoryChanges::~DirectoryChanges()
{
	DestroyOverlapped(ov_buffer_);

	if (owns_directory_)
	{
		::CloseHandle(directory_);
	}
}

void DirectoryChanges::start_watch()
{
	// See ::ReadDirectoryChangesExW(): starting from Windows 10
	const BOOL status = ::ReadDirectoryChangesW(directory_
		, buffer_, length_, watch_sub_tree_, notify_filter_
		, nullptr, &GetOverlapped(ov_buffer_), nullptr);
	if (!status)
	{
		throw_last_error<DirectoryChangesError>("[Dc] ::ReadDirectoryChangesW()");
	}
}
