#include <gtest/gtest.h>
#include <win_io_coro/io_coro_scheduler.h>

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

using namespace wi;

namespace
{

	// Fake awaitable task to introduce coroutine context.
	// Does nothing, holds in-progress status
	struct TestTask
	{
	public:
		struct Token {};

	private:
		struct Promise
		{
			Promise()
				: is_finished_()
			{
			}

			std::experimental::suspend_never initial_suspend()
			{
				return {};
			}

			std::experimental::suspend_never final_suspend()
			{
				if (auto finished = is_finished_.lock())
				{
					*finished = true;
				}
				return {};
			}

			void return_value(Token)
			{
			}

			TestTask get_return_object()
			{
				TestTask task;
				is_finished_ = task.is_finished_;
				return task;
			}

		private:
			std::weak_ptr<std::atomic_bool> is_finished_;
		};

	public:
		using promise_type = Promise;

		bool is_finished() const
		{
			return *is_finished_;
		}

	private:
		explicit TestTask()
			: is_finished_(std::make_shared<std::atomic_bool>(false))
		{
		}

	private:
		// Needs to be shared between Promise and Task
		std::shared_ptr<std::atomic_bool> is_finished_;
	};

} // namespace

TEST(Coro, Fake_TestTask_Compiles_With_Co_Return)
{
	auto coro = []() -> TestTask
	{
		co_return TestTask::Token();
	};
#if !defined(NDEBUG)
	auto task = coro();
	ASSERT_TRUE(task.is_finished());
#else
	(void)coro;
	// #TODO: in Release configuration VS fails with ICE:
	// 
	// test_io_coro_scheduler.cpp(49) : fatal error C1001 : An internal error has occurred in the compiler.
	// 1 > (compiler file 'f:\dd\vctools\compiler\utc\src\p2\main.c', line 260)
	//
	// Report the issue
#endif
}

TEST(Coro, IoScheduler_Poll_One_Returns_Zero_When_No_Task_Was_Created)
{
	detail::IoCompletionPort io_port;
	coro::IoScheduler scheduler(io_port);
	ASSERT_EQ(0u, scheduler.poll_one());
}

TEST(Coro, IoScheduler_Poll_One_Returns_Zero_When_Data_Exists_But_No_Task_Was_Created)
{
	detail::IoCompletionPort io_port;
	coro::IoScheduler scheduler(io_port);

	io_port.post(detail::PortData(1));

	ASSERT_EQ(0u, scheduler.poll_one());

	ASSERT_TRUE(io_port.query().has_value());
}

TEST(Coro, Coroutine_Is_Suspended_When_Waiting_For_Io_Task)
{
	detail::IoCompletionPort io_port;
	coro::IoScheduler scheduler(io_port);

	bool started = false;
	auto work = [&]() -> TestTask
	{
		started = true;
		auto data = co_await scheduler.get();
		(void)data;
		co_return TestTask::Token();
	};

	ASSERT_FALSE(started);
	auto task = work();
	ASSERT_TRUE(started);
	ASSERT_FALSE(task.is_finished());

	io_port.post(detail::PortData(1));
	ASSERT_FALSE(task.is_finished());

	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_TRUE(task.is_finished());
}

TEST(Coro, Await_On_Io_Task_Returns_Posted_Data)
{
	detail::IoCompletionPort io_port;
	coro::IoScheduler scheduler(io_port);

	const detail::PortData post_data(2);
	detail::PortData await_data;

	auto work = [&]() -> TestTask
	{
		await_data = co_await scheduler.get();
		co_return TestTask::Token();
	};

	auto task = work();
	ASSERT_NE(post_data, await_data);
	ASSERT_FALSE(task.is_finished());

	io_port.post(post_data);
	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_EQ(post_data, await_data);
	ASSERT_TRUE(task.is_finished());
}

TEST(Coro, Its_Possible_To_Await_On_More_Then_One_Io_Task_In_The_Same_Coro)
{
	detail::IoCompletionPort io_port;
	coro::IoScheduler scheduler(io_port);

	enum class State
	{
		None,
		Entered,
		Wait_1_Finished,
		Wait_2_Finished,
	};
	State state = State::None;
	detail::PortData last_data;

	auto work = [&]() -> TestTask
	{
		state = State::Entered;
		auto data = co_await scheduler.get();
		(void)data;
		state = State::Wait_1_Finished;
		last_data = co_await scheduler.get();
		state = State::Wait_2_Finished;
		co_return TestTask::Token();
	};

	ASSERT_EQ(State::None, state);
	auto task = work();
	ASSERT_EQ(State::Entered, state);
	ASSERT_FALSE(task.is_finished());

	io_port.post(detail::PortData(3));
	
	ASSERT_EQ(State::Entered, state);
	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_EQ(State::Wait_1_Finished, state);
	ASSERT_FALSE(task.is_finished());

	io_port.post(detail::PortData(4));

	ASSERT_EQ(State::Wait_1_Finished, state);
	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_EQ(State::Wait_2_Finished, state);
	ASSERT_TRUE(task.is_finished());

	ASSERT_EQ(detail::PortData(4), last_data);
}

TEST(Coro, Await_From_Multiple_Threads_Is_Safe)
{
	constexpr std::size_t k_coro_threads_count = 10;

	detail::IoCompletionPort io_port;
	coro::IoScheduler scheduler(io_port);
	std::atomic_bool start(false);
	std::atomic_size_t finished_count(0);

	auto awaiter = [&scheduler]() -> TestTask
	{
		auto data = co_await scheduler.get();
		(void)data;
		co_return TestTask::Token();
	};

	auto worker = [=, &start, &finished_count]()
	{
		while (!start)
		{
			std::this_thread::yield();
		}
		auto task = awaiter();
		while (!task.is_finished())
		{
			std::this_thread::yield();
		}
		++finished_count;
	};

	std::vector<std::thread> coros;
	for (std::size_t i = 0; i < k_coro_threads_count; ++i)
	{
		coros.emplace_back(worker);
	}

	start = true;
	// Wait a little bit while coroutine will be invoked
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	for (std::size_t i = 0; i < k_coro_threads_count; ++i)
	{
		ASSERT_EQ(i, finished_count);
		io_port.post(detail::PortData(1));
		ASSERT_EQ(i, finished_count);
		ASSERT_EQ(1u, scheduler.poll_one());
		
		// Wait a little bit while result will be processed
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		ASSERT_EQ((i + 1), finished_count);
	}

	for (auto& coro : coros)
	{
		coro.join();
	}
}

