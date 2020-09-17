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
    } //namespace detail
} // namespace wi

