#pragma once
#include <atomic>

#include <type_traits>

#include <cassert>

namespace wi
{
    namespace coro
    {
        namespace detail
        {
            // Lock-free, intrusive, non-owning queue.
            // (Thread-safe pointers-like container).
            // 
            // Note: push() and pop() functions are modified versions of the queue
            // from cppcoro: https://github.com/lewissbaker/cppcoro/blob/master/lib/io_service.cpp
            // 
             // #TODO: learn better implemetation. Now, we do iterate thru all
            // elements to get last one (first that was added)

            template<typename T>
            class IntrusiveQueue
            {
            public:
                struct Item
                {
                private:
                    friend class IntrusiveQueue;
                    T* next = nullptr;
                };

            public:
                IntrusiveQueue();
                IntrusiveQueue(const IntrusiveQueue& rhs) = delete;
                IntrusiveQueue& operator=(const IntrusiveQueue& rhs) = delete;
                IntrusiveQueue(IntrusiveQueue*& rhs) = delete;
                IntrusiveQueue& operator=(IntrusiveQueue*& rhs) = delete;

                void push(T* value);
                bool pop(T*& value);

                bool is_empty() const;

            private:
                std::atomic<T*> head_;
            };
        } // namespace detail
    } // namespace coro
} // namespace wi

namespace wi
{
    namespace coro
    {
        namespace detail
        {

            template<typename T>
            IntrusiveQueue<T>::IntrusiveQueue()
                : head_(nullptr)
            {
                static_assert(std::is_base_of<Item, T>::value,
                    "T should be derived from Queue<T>::Item to stored in the queue");
            }

            template<typename T>
            bool IntrusiveQueue<T>::is_empty() const
            {
                return (head_ == nullptr);
            }

            template<typename T>
            void IntrusiveQueue<T>::push(T* value)
            {
                assert(value);
                T* head = head_.load(std::memory_order_acquire);
                do
                {
                    value->next = head;
                }
                while (!head_.compare_exchange_weak(
                    head,
                    value,
                    std::memory_order_release,
                    std::memory_order_acquire));
            }
            
            template<typename T>
            bool IntrusiveQueue<T>::pop(T*& value)
            {
                value = nullptr;
                T* local_head = head_.exchange(nullptr, std::memory_order_acquire);
                if (!local_head)
                {
                    // The queue is empty
                    return false;
                }

                T* tail = local_head;
                while (tail->next)
                {
                    // Find & erase last element
                    const bool before_last = (tail->next->next == nullptr);
                    if (before_last)
                    {
                        std::swap(value, tail->next);
                        break;
                    }
                    tail = tail->next;
                }
                if ((tail == local_head) && (value == nullptr))
                {
                    // The queue contains only one element
                    std::swap(value, local_head);
                    return true;
                }

                // Merge old queue with (possibly) new one
                T* new_head = nullptr;
                while (!head_.compare_exchange_weak(
                    new_head,
                    local_head,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    tail->next = new_head;
                }

                return true;
            }

        } // namespace detail
    } // namespace coro
} // namespace wi
