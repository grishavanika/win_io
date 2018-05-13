#pragma once
#include <win_io/detail/win_types.h>

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

		private:
			WinHANDLE io_port_;
		};

	} // namespace detail
} // namespace wi

