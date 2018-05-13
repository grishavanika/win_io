#pragma once
#include <win_io/errors.h>

namespace wi
{
	namespace detail
	{

		class IoCompletionPortError : public Error
		{
		public:
			using Error::Error;
		};

	} // namespace detail
} // namespace wi

