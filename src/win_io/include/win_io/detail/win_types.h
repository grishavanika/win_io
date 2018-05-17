#pragma once
#include <cstdint>

namespace wi
{
	namespace detail
	{
		// You will have compile time error if mismatch
		// with real Win API typles from Windows.h will
		// be detected
		using WinHANDLE = void*;
		using WinSOCKET = void*;
		using WinDWORD = std::uint32_t;
		using WinULONG_PTR = std::uintptr_t;
		using WinOVERLAPPEDBuffer = void*[4];
	} // namespace detail
} // namespace wi
