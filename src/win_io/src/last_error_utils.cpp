#include <win_io/detail/last_error_utils.h>

#include <Windows.h>

namespace wi
{
	namespace detail
	{
		std::uint32_t GetLastWinError()
		{
			return ::GetLastError();
		}
	}
}
