#pragma once
#include <win_io/detail/io_completion_port_errors.h>
#include <win_io/detail/last_error_utils.h>

#include <chrono>

#include <cstdint>

namespace wi
{
    namespace detail
    {

        // Low-level wrapper around Windows I/O Completion Port
        class IoCompletionPort
        {
        public:
            IoCompletionPort();
            IoCompletionPort(std::uint32_t concurrent_threads_hint);
            ~IoCompletionPort();

            IoCompletionPort(const IoCompletionPort&) = delete;
            IoCompletionPort& operator=(const IoCompletionPort&) = delete;
            IoCompletionPort(IoCompletionPort&&) = delete;
            IoCompletionPort& operator=(IoCompletionPort&&) = delete;

            void post(const PortData& data, std::error_code& ec);
            void post(const PortData& data);

            // Blocking call.
            // It's possible to have valid data, but still receive some `error_code`.
            // See https://xania.org/200807/iocp article for possible
            // combination of results from the call to ::GetQueuedCompletionStatus()
            std::optional<PortData> get(std::error_code& ec);
            PortData get();

            // Non-blocking call
            std::optional<PortData> query(std::error_code& ec);
            std::optional<PortData> query();

            // Blocking call with time-out
            template<typename Rep, typename Period>
            std::optional<PortData> wait_for(std::chrono::duration<Rep, Period> time
                , std::error_code& ec);
            template<typename Rep, typename Period>
            std::optional<PortData> wait_for(std::chrono::duration<Rep, Period> time);

            void associate_device(WinHANDLE device, WinULONG_PTR key
                , std::error_code& ec);
            void associate_device(WinHANDLE device, WinULONG_PTR key);

            void associate_socket(WinSOCKET socket, WinULONG_PTR key
                , std::error_code& ec);
            void associate_socket(WinSOCKET socket, WinULONG_PTR key);

            WinHANDLE native_handle();

        private:
            std::optional<PortData> wait_impl(WinDWORD milliseconds, std::error_code& ec);
            void associate_with_impl(WinHANDLE device, WinULONG_PTR key
                , std::error_code& ec);
            void associate_with_impl(WinHANDLE device, WinULONG_PTR key);

        private:
            WinHANDLE io_port_;
        };

    } // namespace detail
} // namespace wi

namespace wi
{
    namespace detail
    {

        template<typename Rep, typename Period>
        std::optional<PortData> IoCompletionPort::wait_for(
            std::chrono::duration<Rep, Period> time, std::error_code& ec)
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time);
            return wait_impl(static_cast<WinDWORD>(ms.count()), ec);
        }

        template<typename Rep, typename Period>
        std::optional<PortData> IoCompletionPort::wait_for(std::chrono::duration<Rep, Period> time)
        {
            std::error_code ec;
            auto data = wait_for(std::move(time), ec);
            if (ec)
            {
                throw_error<IoCompletionPortQueryError>(ec, "[Io] wait_for() failed"
                    , std::move(data));
            }
            return data;
        }

    } // namespace detail
} // namespace wi

