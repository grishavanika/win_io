#pragma once
#include <win_io/detail/io_completion_port.h>
#include <win_io_coro/io_task.h>

#include <cstddef>

namespace wi
{
	namespace coro
	{
		using IoCompletionPort = ::wi::detail::IoCompletionPort;

		class IoScheduler
		{
		public:
			IoScheduler(IoCompletionPort& io_port);
		
			std::size_t poll();
			std::size_t poll_one();

			std::size_t run();
			std::size_t run_one();

			// Should be used together with co_await.
			// Suspends coroutine until any IoCompletionPort data
			// becomes available
			IoTask get();

		private:
			friend class IoTask;
			void add(IoTask& task);

		private:
			IoCompletionPort& io_port_;
			detail::IntrusiveQueue<IoTask> tasks_;
		};

	} // namespace coro
} // namespace wi

