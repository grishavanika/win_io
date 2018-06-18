#include <gtest/gtest.h>
#include <win_io_coro/io_coro_scheduler.h>

using namespace wi;

namespace
{

	// Fake awaitable task to introduce coroutine context.
	// Does nothing, holds nothing
	struct TestTask
	{
		struct Token
		{
		};

		struct Promise
		{
			std::experimental::suspend_never initial_suspend()
			{
				return {};
			}

			std::experimental::suspend_never final_suspend()
			{
				return {};
			}

			void return_value(Token)
			{
			}

			TestTask get_return_object()
			{
				return {};
			}
		};

		using promise_type = Promise;
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
	(void)task;
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
	bool finished = false;
	auto work = [&]() -> TestTask
	{
		started = true;
		auto data = co_await scheduler.get();
		(void)data;
		finished = true;
		co_return TestTask::Token();
	};

	ASSERT_FALSE(started);
	ASSERT_FALSE(finished);
	(void)work();
	ASSERT_TRUE(started);
	ASSERT_FALSE(finished);

	io_port.post(detail::PortData(1));
	ASSERT_FALSE(finished);

	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_TRUE(finished);
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

	io_port.post(post_data);
	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_EQ(post_data, await_data);
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

	io_port.post(detail::PortData(3));
	
	ASSERT_EQ(State::Entered, state);
	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_EQ(State::Wait_1_Finished, state);

	io_port.post(detail::PortData(4));

	ASSERT_EQ(State::Wait_1_Finished, state);
	ASSERT_EQ(1u, scheduler.poll_one());
	ASSERT_EQ(State::Wait_2_Finished, state);

	ASSERT_EQ(detail::PortData(4), last_data);
}
