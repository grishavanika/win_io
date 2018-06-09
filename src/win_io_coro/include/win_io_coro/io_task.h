#pragma once
#include <win_io/detail/io_completion_port.h>

#include <experimental/coroutine>

namespace wi
{
	namespace coro
	{
		class IoScheduler;

		// Awaitable task with minimal interface
		// that allows to fetch `IoCompletionPort` data
		// in the coroutine context.
		// Warning: no any error handling supported
		class IoTask
		{
		public:
			IoTask(IoScheduler& scheduler);

			IoTask(IoTask&& rhs) noexcept;
			IoTask& operator=(IoTask&& rhs) = delete;
			IoTask(const IoTask& rhs) = delete;
			IoTask& operator=(const IoTask& rhs) = delete;

			bool await_ready() const noexcept;
			void await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept;
			detail::PortData await_resume() noexcept;

			// Any coroutine that awaits on the task will be resumed
			// with given `data` passed in
			void set(detail::PortData data);

		private:
			IoScheduler* scheduler_;
			std::experimental::coroutine_handle<> coro_;
			detail::PortData data_;
		};

	} // namespace coro
} // namespace wi
