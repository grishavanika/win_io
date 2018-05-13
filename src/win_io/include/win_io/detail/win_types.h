#pragma once
#include <cstdint>

namespace wi
{
	namespace detail
	{

		using WinHANDLE = void*;
		using WinDWORD = std::uint32_t;
		using WinULONG_PTR = std::uintptr_t;

	} // namespace detail
} // namespace wi
