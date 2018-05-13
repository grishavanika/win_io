#pragma once
#include <win_io/detail/win_types.h>

namespace wi
{
	namespace detail
	{

		struct PortData
		{
			WinDWORD value = 0;
			WinULONG_PTR key = 0;
			void* ptr = nullptr;

			PortData(WinDWORD value = 0, WinULONG_PTR key = 0, void* ptr = nullptr);
		};

		bool operator==(const PortData& lhs, const PortData& rhs);

	} // namespace detail
} // namespace wi

namespace wi
{
	namespace detail
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

	} // namespace detail
} // namespace wi
