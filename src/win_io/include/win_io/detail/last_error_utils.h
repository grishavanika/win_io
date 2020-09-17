#pragma once
#include <win_io/detail/win_types.h>

#include <system_error>
#include <type_traits>

#include <cstdint>

namespace wi
{
    namespace detail
    {
        WinDWORD GetLastWinError();

        inline std::error_code make_last_error_code(
            WinDWORD last_error = GetLastWinError())
        {
            // Using `system_category` with implicit assumption that
            // MSVC's implementation will add proper error code message for free
            // if using together with `std::system_error`
            return std::error_code(static_cast<int>(last_error), std::system_category());
        }

        template<typename E>
        using ModelsSystemError = std::enable_if_t<
            std::is_base_of<std::system_error, E>::value>;

        template<typename E, typename... Args
            , typename = ModelsSystemError<E>>
        [[noreturn]] void throw_error(std::error_code ec, Args&&... args)
        {
            throw E(ec, std::forward<Args>(args)...);
        }

        template<typename E, typename... Args
            , typename = ModelsSystemError<E>>
        [[noreturn]] void throw_error(const char* message, std::error_code ec
            , Args&&... args)
        {
            throw E(ec, message);
        }

        template<typename E>
        [[noreturn]] void throw_last_error(
            WinDWORD last_error = GetLastWinError())
        {
            throw_error<E>(make_last_error_code(last_error));
        }

        template<typename E>
        [[noreturn]] void throw_last_error(const char* message
            , WinDWORD last_error = GetLastWinError())
        {
            throw_error<E>(message, make_last_error_code(last_error));
        }

        template<typename E>
        void throw_if_error(std::error_code ec)
        {
            if (ec)
            {
                throw_error<E>(ec);
            }
        }

        template<typename E>
        void throw_if_error(const char* message, std::error_code ec)
        {
            if (ec)
            {
                throw_error<E>(message, ec);
            }
        }

    } //namespace detail
} // namespace wi

