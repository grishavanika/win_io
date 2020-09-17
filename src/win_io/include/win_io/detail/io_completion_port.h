#pragma once
#include <win_io/detail/io_completion_port_data.h>

#include <chrono>
#include <system_error>
#include <optional>

#include <cstdint>

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

