#include <win_io/detail/last_error_utils.h>

#include <Windows.h>

namespace wi
{
    namespace detail
    {
        WinDWORD GetLastWinError()
        {
            return ::GetLastError();
        }
    }
}
