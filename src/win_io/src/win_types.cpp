#include <win_io/detail/win_types.h>

#include <Windows.h>

namespace wi
{
	namespace detail
	{
		namespace
		{

			static_assert(sizeof(WinOVERLAPPEDBuffer) == sizeof(OVERLAPPED)
				, "Mismatch in OVERLAPPED size detected");
			static_assert(alignof(WinOVERLAPPEDBuffer) == alignof(OVERLAPPED)
				, "Mismatch in OVERLAPPED align detected");

		} // namespace
	} // namespace detail
} // namespace wi
