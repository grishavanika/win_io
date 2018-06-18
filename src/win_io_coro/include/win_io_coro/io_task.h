#pragma once
#include <win_io/detail/io_completion_port.h>

#include <win_io_coro/detail/intrusive_queue.h>

#include <experimental/coroutine>

namespace wi
{
	namespace coro
	{
		using PortData = ::wi::detail::PortData;

		class IoScheduler;

		// Awaitable task with minimal interface
		// that allows to fetch `IoCompletionPort` data
		// in the coroutine context.
		// Warning: no any error handling supported
		class IoTask : public detail::IntrusiveQueue<IoTask>::Item
		{
		public:
			IoTask(IoScheduler& scheduler);
			~IoTask();

			IoTask(IoTask&& rhs) noexcept;
			IoTask& operator=(IoTask&& rhs) = delete;
			IoTask(const IoTask& rhs) = delete;
			IoTask& operator=(const IoTask& rhs) = delete;

			bool await_ready() const noexcept;
			void await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept;
			PortData await_resume() noexcept;

		private:
			friend class IoScheduler;
			// Any coroutine that awaits on the task will be resumed
			// with given `data` passed in
			void set(PortData data);

		private:
			IoScheduler* scheduler_;
			std::experimental::coroutine_handle<> coro_;
			PortData data_;
		};

	} // namespace coro
} // namespace wi
