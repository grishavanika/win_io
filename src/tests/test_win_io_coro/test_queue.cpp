#include <gtest/gtest.h>
#include <win_io_coro/detail/queue.h>

#include <vector>
#include <thread>
#include <algorithm>
#include <random>

using namespace wi::coro::detail;

namespace
{

	struct TestElement : IntrusiveQueue<TestElement>::Item
	{
		int value;
		
		TestElement(int v)
			: value(v)
		{
		}
	};

	bool operator==(const TestElement& lhs, const TestElement& rhs)
	{
		return (lhs.value == rhs.value);
	}

	auto GetSeed()
	{
		static std::random_device rd;
		return rd();
	}

	using milliseconds = std::chrono::milliseconds;
	using namespace std::chrono_literals;

	milliseconds GetRandomDurantion(milliseconds from, milliseconds to)
	{
		std::uniform_int_distribution<milliseconds::rep> dist(from.count(), to.count());
		std::mt19937 gen(GetSeed());
		return milliseconds(dist(gen));
	}

	void SleepRandomly(milliseconds from = 10ms, milliseconds to = 20ms)
	{
		std::this_thread::sleep_for(GetRandomDurantion(from, to));
	}

} // namespace

TEST(Queue, Default_Constructed_Queue_Has_No_Elements)
{
	IntrusiveQueue<TestElement> queue;
	TestElement* element = nullptr;
	ASSERT_FALSE(queue.pop(element));
	ASSERT_EQ(nullptr, element);
}

TEST(Queue, Pop_After_Single_Push_Returns_Pushed_Element)
{
	IntrusiveQueue<TestElement> queue;

	TestElement value(1);
	queue.push(&value);

	TestElement* ptr = nullptr;
	ASSERT_TRUE(queue.pop(ptr));
	ASSERT_EQ(&value, ptr);

	ASSERT_FALSE(queue.pop(ptr));
}

TEST(Queue, Pop_Returns_In_FIFO_Order)
{
	IntrusiveQueue<TestElement> queue;

	TestElement value1(1);
	TestElement value2(2);
	queue.push(&value1);
	queue.push(&value2);

	TestElement* ptr = nullptr;
	ASSERT_TRUE(queue.pop(ptr));
	ASSERT_EQ(&value1, ptr);

	ASSERT_TRUE(queue.pop(ptr));
	ASSERT_EQ(&value2, ptr);
}

TEST(Queue, Popped_Elements_From_Single_Thread_Are_Same_As_Input)
{
	constexpr int k_elements_count = 100;

	std::vector<TestElement> input;
	for (int i = 0; i < k_elements_count; ++i)
	{
		input.emplace_back(i + 1);
	}

	IntrusiveQueue<TestElement> queue;
	for (auto& element : input)
	{
		queue.push(&element);
	}

	std::vector<TestElement> output;
	TestElement* element = nullptr;
	while (queue.pop(element))
	{
		ASSERT_NE(nullptr, element);
		output.push_back(*element);
	}

	ASSERT_EQ(input, output);
}

TEST(Queue, Popped_Elements_From_Multiple_Threads_Are_Same_As_Input)
{
	using Elements = std::vector<TestElement>;
	constexpr std::size_t k_workers_count = 5;
	constexpr int k_elements_count = 100;

	Elements input;
	for (int i = 0; i < k_elements_count; ++i)
	{
		input.emplace_back(i + 1);
	}

	IntrusiveQueue<TestElement> queue;
	for (auto& element : input)
	{
		queue.push(&element);
	}

	std::vector<std::thread> workers;
	std::vector<Elements> elements_per_thread;
	workers.reserve(k_workers_count);
	elements_per_thread.resize(k_workers_count);

	std::atomic_bool start(false);

	for (std::size_t i = 0; i < k_workers_count; ++i)
	{
		workers.emplace_back([&, i]()
		{
			// Spin-wait for start and some random delay is the best found way
			// to distribute CPU time more-or-less consistently between threads
			// (at least on Win 10, Visual Studio 17)
			while (!start)
			{
				std::this_thread::yield();
			}
			SleepRandomly();

			auto& elements = elements_per_thread[i];
			TestElement* element = nullptr;
			while (queue.pop(element))
			{
				elements.push_back(*element);
				SleepRandomly();
			}
		});
	}

	start = true;
	
	for (auto& worker : workers)
	{
		worker.join();
	}

	std::vector<TestElement> output;
	for (auto& elements : elements_per_thread)
	{
		output.insert(output.end(), elements.begin(), elements.end());
	}

	ASSERT_TRUE(std::is_permutation(
		output.begin(), output.end()
		, input.begin(), input.end()));
}
