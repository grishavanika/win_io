#include <win_io/detail/io_completion_port.h>

#include <limits>

#include <cassert>

#include <Windows.h>

using namespace wi;
using namespace detail;

IoCompletionPort::IoCompletionPort()
	// Do not limit number of threads by default
	: IoCompletionPort((std::numeric_limits<std::uint32_t>::max)())
{
}

IoCompletionPort::IoCompletionPort(std::uint32_t concurrent_threads_hint)
	: io_port_(nullptr)
{
	io_port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE
		, nullptr // Create new one
		, 0	// No completion key yet
		, concurrent_threads_hint);

	if (!io_port_)
	{
		throw_last_error<IoCompletionPortError>("[Io] ::CreateIoCompletionPort()");
	}
}

IoCompletionPort::~IoCompletionPort()
{
	const bool ok = !!::CloseHandle(io_port_);
	assert(ok && "[Io] ::CloseHandle() on IoCompletionPort failed");
	(void)ok;
}

void IoCompletionPort::post(const PortData& data)
{
	std::error_code ec;
	post(data, ec);
	throw_if_error<IoCompletionPortError>("[Io] post() failed", ec);
}

void IoCompletionPort::post(const PortData& data, std::error_code& ec)
{
	ec = std::error_code();
	const BOOL ok = ::PostQueuedCompletionStatus(io_port_, data.value, data.key
		, static_cast<LPOVERLAPPED>(data.ptr));
	if (!ok)
	{
		ec = make_last_error_code();
	}
}

nonstd::optional<PortData> IoCompletionPort::get(std::error_code& ec)
{
	return wait_impl(INFINITE, ec);
}

PortData IoCompletionPort::get()
{
	std::error_code ec;
	auto data = get(ec);
	if (ec)
	{
		throw_error<IoCompletionPortQueryError>(ec, "[Io] post() failed"
			, std::move(data));
	}
	return *data;
}

nonstd::optional<PortData> IoCompletionPort::query(std::error_code& ec)
{
	return wait_impl(0/*no blocking wait*/, ec);
}

nonstd::optional<PortData> IoCompletionPort::query()
{
	std::error_code ec;
	auto data = query(ec);
	if (ec)
	{
		throw_error<IoCompletionPortQueryError>(ec, "[Io] query() failed"
			, std::move(data));
	}
	return data;
}

void IoCompletionPort::associate_device(WinHANDLE device, WinULONG_PTR key
	, std::error_code& ec)
{
	associate_with_impl(device, key, ec);
}

void IoCompletionPort::associate_device(WinHANDLE device, WinULONG_PTR key)
{
	associate_with_impl(device, key);
}

void IoCompletionPort::associate_socket(WinSOCKET socket, WinULONG_PTR key
	, std::error_code& ec)
{
	associate_with_impl(socket, key, ec);
}

void IoCompletionPort::associate_socket(WinSOCKET socket, WinULONG_PTR key)
{
	associate_with_impl(socket, key);
}

nonstd::optional<PortData> IoCompletionPort::wait_impl(
	WinDWORD milliseconds, std::error_code& ec)
{
	DWORD bytes_transferred = 0;
	ULONG_PTR completion_key = 0;
	LPOVERLAPPED overlapped = nullptr;

	const BOOL status = ::GetQueuedCompletionStatus(io_port_
		, &bytes_transferred, &completion_key, &overlapped, milliseconds);
	PortData data(bytes_transferred, completion_key, overlapped);

	if (status)
	{
		ec = std::error_code();
		return data;
	}

	ec = make_last_error_code();
	if (overlapped)
	{
		return data;
	}
	return nonstd::nullopt;
}

void IoCompletionPort::associate_with_impl(
	WinHANDLE device, WinULONG_PTR key, std::error_code& ec)
{
	ec = std::error_code();
	const auto this_port = ::CreateIoCompletionPort(device
		, io_port_ // Attach to existing
		, key
		, 0);
	if (!this_port)
	{
		ec = make_last_error_code();
		return;
	}
	assert(this_port == io_port_ && "[Io] Expected to have same Io Port");
}

void IoCompletionPort::associate_with_impl(WinHANDLE device, WinULONG_PTR key)
{
	std::error_code ec;
	associate_with_impl(device, key, ec);
	throw_if_error<IoCompletionPortError>("[Io] associate() failed", ec);
}

WinHANDLE IoCompletionPort::native_handle()
{
	return io_port_;
}
