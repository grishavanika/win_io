#include <gtest/gtest.h>
#include <win_io_coro/detail/intrusive_queue.h>

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

    using Elements = std::vector<TestElement>;

    Elements MakeElements(std::size_t count)
    {
        Elements elements;
        for (std::size_t i = 0; i < count; ++i)
        {
            elements.emplace_back(static_cast<int>(i + 1));
        }
        return elements;
    }

    Elements FlattenElements(std::vector<Elements> data)
    {
        Elements output;
        for (auto& elements : data)
        {
            output.insert(output.end(), elements.begin(), elements.end());
        }
        return output;
    }

    void WaitAll(std::vector<std::thread>& threads)
    {
        for (auto& thread : threads)
        {
            thread.join();
        }
    }

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
    constexpr std::size_t k_elements_count = 100;

    std::vector<TestElement> input = MakeElements(k_elements_count);
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
    constexpr std::size_t k_workers_count = 5;
    constexpr std::size_t k_elements_count = 100;

    Elements input = MakeElements(k_elements_count);
    IntrusiveQueue<TestElement> queue;
    for (auto& element : input)
    {
        queue.push(&element);
    }

    std::vector<std::thread> workers;
    std::vector<Elements> output_per_thread;
    workers.reserve(k_workers_count);
    output_per_thread.resize(k_workers_count);

    std::atomic_bool start(false);

    // Spawn consumers threads
    for (std::size_t worker_id = 0; worker_id < k_workers_count; ++worker_id)
    {
        workers.emplace_back([&, worker_id]()
        {
            // Spin-wait for start and some random delay is the best found way
            // to distribute CPU time more-or-less consistently between threads
            // (at least on Win 10, Visual Studio 17)
            while (!start)
            {
                std::this_thread::yield();
            }
            SleepRandomly();

            auto& elements = output_per_thread[worker_id];
            TestElement* element = nullptr;
            while (queue.pop(element))
            {
                elements.push_back(*element);
                SleepRandomly();
            }
        });
    }

    start = true;
    WaitAll(workers);

    Elements output = FlattenElements(std::move(output_per_thread));

    ASSERT_TRUE(std::is_permutation(
        output.begin(), output.end()
        , input.begin(), input.end()));
}

TEST(Queue, Contains_All_Pushed_From_Multiple_Threads_Elements)
{
    constexpr std::size_t k_workers_count = 5;
    constexpr std::size_t k_elements_count = 100;
    static_assert((k_elements_count % k_workers_count) == 0u,
        "Elements count should be equivalent for each thread");
    constexpr std::size_t k_elements_per_thread = (k_elements_count / k_workers_count);

    Elements input = MakeElements(k_elements_count);
    
    std::vector<std::thread> workers;
    workers.reserve(k_workers_count);

    IntrusiveQueue<TestElement> queue;

    std::atomic_bool start(false);

    // Spawn producers threads
    for (std::size_t worker_id = 0; worker_id < k_workers_count; ++worker_id)
    {
        workers.emplace_back([&, worker_id]()
        {
            while (!start)
            {
                std::this_thread::yield();
            }

            for (std::size_t i = 0; i < k_elements_per_thread; ++i)
            {
                TestElement& element = input[worker_id * k_elements_per_thread + i];
                queue.push(&element);
            }
        });
    }

    start = true;
    WaitAll(workers);

    std::vector<TestElement> output;
    TestElement* element = nullptr;
    while (queue.pop(element))
    {
        output.push_back(*element);
    }

    ASSERT_TRUE(std::is_permutation(
        output.begin(), output.end()
        , input.begin(), input.end()));
}

TEST(Queue, Multiple_Producers_Consumers_Threads)
{
    constexpr std::size_t k_producers_count = 5;
    constexpr std::size_t k_consumers_count = 5;
    constexpr std::size_t k_elements_count = 200;
    static_assert((k_elements_count % k_producers_count) == 0u,
        "Elements count should be equivalent for each producer thread");
    constexpr std::size_t k_elements_per_producer = (k_elements_count / k_producers_count);

    Elements input = MakeElements(k_elements_count);
    std::vector<Elements> output_per_consumer;
    output_per_consumer.resize(k_consumers_count);

    std::vector<std::thread> producers;
    producers.reserve(k_producers_count);
    std::vector<std::thread> consumers;
    consumers.reserve(k_consumers_count);

    IntrusiveQueue<TestElement> queue;

    std::atomic_bool start(false);
    std::atomic_bool producers_finished(false);

    // Spawn producers threads
    for (std::size_t producer_id = 0; producer_id < k_producers_count; ++producer_id)
    {
        producers.emplace_back([&, producer_id]()
        {
            while (!start)
            {
                std::this_thread::yield();
            }

            for (std::size_t i = 0; i < k_elements_per_producer; ++i)
            {
                TestElement& element = input[producer_id * k_elements_per_producer + i];
                queue.push(&element);
            }
        });
    }

    // Spawn consumers threads
    for (std::size_t consumer_id = 0; consumer_id < k_consumers_count; ++consumer_id)
    {
        consumers.emplace_back([&, consumer_id]()
        {
            while (!start)
            {
                std::this_thread::yield();
            }

            auto& elements = output_per_consumer[consumer_id];
            while (true)
            {
                TestElement* element = nullptr;
                if (queue.pop(element))
                {
                    elements.push_back(*element);
                }
                else if (producers_finished)
                {
                    break;
                }
            }
        });
    }

    start = true;
    WaitAll(producers);
    producers_finished = true;
    WaitAll(consumers);

    ASSERT_TRUE(queue.is_empty());

    Elements output = FlattenElements(output_per_consumer);

    ASSERT_TRUE(std::is_permutation(
        output.begin(), output.end()
        , input.begin(), input.end()))
        << "Consumed elements are not the same as produced ones. "
        << "Input size: " << input.size() << ". "
        << "Output size: " << output.size();
}

