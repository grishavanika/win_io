#pragma once
#include <chrono>
#include <optional>
#include <system_error> // std::error_code.

#include <cstdint> // std::[u]int*_t.

namespace wi
{
    // You will have compile time error if mismatch
    // with real Win API types from Windows.h will
    // be detected (size and alignment validated).
    using WinHANDLE = void*;
    using WinSOCKET = void*;
    using WinDWORD = std::uint32_t;
    using WinULONG_PTR = std::uintptr_t;

    struct WinOVERLAPPED
    {
        WinULONG_PTR Internal;
        WinULONG_PTR InternalHigh;
        union
        {
            struct
            {
                WinDWORD Offset;
                WinDWORD OffsetHigh;
            } _;
            void* Pointer;
        };
        WinHANDLE hEvent;
    };
} // namespace wi

namespace wi
{
    struct PortData
    {
        WinDWORD value = 0;
        WinULONG_PTR key = 0;
        void* ptr = nullptr;

        PortData(WinDWORD value = 0, WinULONG_PTR key = 0, void* ptr = nullptr);
    };

    bool operator==(const PortData& lhs, const PortData& rhs);
    bool operator!=(const PortData& lhs, const PortData& rhs);
} // namespace wi

namespace wi::detail
{
    WinDWORD GetLastWinError();

    inline std::error_code make_last_error_code(
        WinDWORD last_error = GetLastWinError())
    {
        // Using `system_category` with implicit assumption that
        // MSVC's implementation will add proper error code message for free
        // if using together with `std::system_error`.
        return std::error_code(static_cast<int>(last_error), std::system_category());
    }
} // namespace wi

namespace wi
{
    inline PortData::PortData(WinDWORD v /*= 0*/, WinULONG_PTR k /*= 0*/, void* p /*= nullptr*/)
        : value(v)
        , key(k)
        , ptr(p)
    {
    }

    inline bool operator==(const PortData& lhs, const PortData& rhs)
    {
        return ((lhs.value == rhs.value)
            && (lhs.key == rhs.key)
            && (lhs.ptr == rhs.ptr));
    }

    inline bool operator!=(const PortData& lhs, const PortData& rhs)
    {
        return !(lhs == rhs);
    }
} // namespace wi

namespace wi
{
    // Low-level wrapper around Windows I/O Completion Port.
    class IoCompletionPort
    {
    public:
        static std::optional<IoCompletionPort> make(std::error_code& ec) noexcept;
        static std::optional<IoCompletionPort> make(std::uint32_t concurrent_threads_hint
            , std::error_code& ec) noexcept;

        // Construct invalid object. Same as moved-from state.
        explicit IoCompletionPort() noexcept = default;
        IoCompletionPort(const IoCompletionPort&) = delete;
        IoCompletionPort& operator=(const IoCompletionPort&) = delete;
        IoCompletionPort(IoCompletionPort&& rhs) noexcept;
        IoCompletionPort& operator=(IoCompletionPort&& rhs) noexcept;
        ~IoCompletionPort();

        void post(const PortData& data, std::error_code& ec);

        // Blocking call.
        // It's possible to have valid data, but still receive some `error_code`.
        // See https://xania.org/200807/iocp article for possible
        // combination of results from the call to ::GetQueuedCompletionStatus().
        std::optional<PortData> get(std::error_code& ec);

        // Non-blocking call.
        std::optional<PortData> query(std::error_code& ec);

        // Blocking call with time-out.
        template<typename Rep, typename Period>
        std::optional<PortData> wait_for(std::chrono::duration<Rep, Period> time
            , std::error_code& ec);

        void associate_device(WinHANDLE device, WinULONG_PTR key
            , std::error_code& ec);

        void associate_socket(WinSOCKET socket, WinULONG_PTR key
            , std::error_code& ec);

        WinHANDLE native_handle();

    private:
        std::optional<PortData> wait_impl(WinDWORD milliseconds, std::error_code& ec);
        void associate_with_impl(WinHANDLE device, WinULONG_PTR key
            , std::error_code& ec);
        void close() noexcept;

    private:
        WinHANDLE io_port_ = nullptr;
    };
} // namespace wi

namespace wi
{
    template<typename Rep, typename Period>
    std::optional<PortData> IoCompletionPort::wait_for(
        std::chrono::duration<Rep, Period> time, std::error_code& ec)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time);
        return wait_impl(static_cast<WinDWORD>(ms.count()), ec);
    }
} // namespace wi

///////////////////////////////////////////////////////////////////////////////
// Implementation that was previously in .cpp file.
// 
#include <utility>

#include <cassert>

#include <Windows.h>

namespace wi
{
    static_assert(sizeof(WinOVERLAPPED) == sizeof(OVERLAPPED)
        , "Mismatch in OVERLAPPED size detected.");
    static_assert(alignof(WinOVERLAPPED) == alignof(OVERLAPPED)
        , "Mismatch in OVERLAPPED align detected.");
} // namespace wi

namespace wi::detail
{
    inline WinDWORD GetLastWinError()
    {
        return ::GetLastError();
    }
}

namespace wi
{
    /*static*/ inline std::optional<IoCompletionPort> IoCompletionPort::make(std::error_code& ec) noexcept
    {
        // As many concurrently running threads as there are processors in the system.
        return make(0, ec);
    }

    /*static*/ inline std::optional<IoCompletionPort> IoCompletionPort::make(std::uint32_t concurrent_threads_hint
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
            ec = detail::make_last_error_code();
            return std::nullopt;
        }

        return value;
    }

    inline IoCompletionPort::IoCompletionPort(IoCompletionPort&& rhs) noexcept
        : io_port_(std::exchange(rhs.io_port_, nullptr))
    {
    }

    inline IoCompletionPort& IoCompletionPort::operator=(IoCompletionPort&& rhs) noexcept
    {
        if (this != &rhs)
        {
            close();
            io_port_ = std::exchange(rhs.io_port_, nullptr);
        }
        return *this;
    }

    inline IoCompletionPort::~IoCompletionPort()
    {
        close();
    }

    inline void IoCompletionPort::close() noexcept
    {
        if (io_port_)
        {
            [[maybe_unused]] const bool ok = !!::CloseHandle(io_port_);
            assert(ok && "[Io] ::CloseHandle() on IoCompletionPort failed");
            io_port_ = nullptr;
        }
    }

    inline void IoCompletionPort::post(const PortData& data, std::error_code& ec)
    {
        ec = std::error_code();
        const BOOL ok = ::PostQueuedCompletionStatus(io_port_, data.value, data.key
            , static_cast<LPOVERLAPPED>(data.ptr));
        if (!ok)
        {
            ec = detail::make_last_error_code();
        }
    }

    inline std::optional<PortData> IoCompletionPort::get(std::error_code& ec)
    {
        return wait_impl(INFINITE, ec);
    }

    inline std::optional<PortData> IoCompletionPort::query(std::error_code& ec)
    {
        return wait_impl(0/*no blocking wait*/, ec);
    }

    inline void IoCompletionPort::associate_device(WinHANDLE device, WinULONG_PTR key
        , std::error_code& ec)
    {
        associate_with_impl(device, key, ec);
    }

    inline void IoCompletionPort::associate_socket(WinSOCKET socket, WinULONG_PTR key
        , std::error_code& ec)
    {
        associate_with_impl(socket, key, ec);
    }

    inline std::optional<PortData> IoCompletionPort::wait_impl(
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

        ec = detail::make_last_error_code();
        if (overlapped)
        {
            return data;
        }
        return std::nullopt;
    }

    inline void IoCompletionPort::associate_with_impl(
        WinHANDLE device, WinULONG_PTR key, std::error_code& ec)
    {
        ec = std::error_code();
        const auto this_port = ::CreateIoCompletionPort(device
            , io_port_ // Attach to existing
            , key
            , 0);
        if (!this_port)
        {
            ec = detail::make_last_error_code();
            return;
        }
        assert(this_port == io_port_ && "[Io] Expected to have same Io Port");
    }

    inline WinHANDLE IoCompletionPort::native_handle()
    {
        return io_port_;
    }
} // namespace wi
