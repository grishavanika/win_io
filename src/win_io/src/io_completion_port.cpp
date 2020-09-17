#include <win_io/detail/io_completion_port.h>
#include <win_io/detail/last_error_utils.h>

#include <limits>

#include <cassert>

#include <Windows.h>

using namespace wi;
using namespace detail;

/*static*/ std::optional<IoCompletionPort> IoCompletionPort::make(std::error_code& ec) noexcept
{
    // As many concurrently running threads as there are processors in the system.
    return make(0, ec);
}

/*static*/ std::optional<IoCompletionPort> IoCompletionPort::make(std::uint32_t concurrent_threads_hint
    , std::error_code& ec) noexcept
{
    std::optional<IoCompletionPort> value;

    IoCompletionPort& o = value.emplace();
    o.io_port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE
        , nullptr // Create new one.
        , 0       // No completion key yet.
        , concurrent_threads_hint);
    if (o.io_port_ == nullptr)
    {
        ec = make_last_error_code();
        return std::nullopt;
    }

    return value;
}

IoCompletionPort::IoCompletionPort(IoCompletionPort&& rhs) noexcept
    : io_port_(std::exchange(rhs.io_port_, nullptr))
{
}

IoCompletionPort& IoCompletionPort::operator=(IoCompletionPort&& rhs) noexcept
{
    if (this != &rhs)
    {
        close();
        io_port_ = std::exchange(rhs.io_port_, nullptr);
    }
    return *this;
}

IoCompletionPort::~IoCompletionPort()
{
    close();
}

void IoCompletionPort::close() noexcept
{
    if (io_port_)
    {
        [[maybe_unused]] const bool ok = !!::CloseHandle(io_port_);
        assert(ok && "[Io] ::CloseHandle() on IoCompletionPort failed");
        io_port_ = nullptr;
    }
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

std::optional<PortData> IoCompletionPort::get(std::error_code& ec)
{
    return wait_impl(INFINITE, ec);
}

std::optional<PortData> IoCompletionPort::query(std::error_code& ec)
{
    return wait_impl(0/*no blocking wait*/, ec);
}

void IoCompletionPort::associate_device(WinHANDLE device, WinULONG_PTR key
    , std::error_code& ec)
{
    associate_with_impl(device, key, ec);
}

void IoCompletionPort::associate_socket(WinSOCKET socket, WinULONG_PTR key
    , std::error_code& ec)
{
    associate_with_impl(socket, key, ec);
}

std::optional<PortData> IoCompletionPort::wait_impl(
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
    return std::nullopt;
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

WinHANDLE IoCompletionPort::native_handle()
{
    return io_port_;
}
