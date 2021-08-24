#include <win_io_coro/io_task.h>
#include <win_io_coro/io_coro_scheduler.h>

#include <cassert>

using namespace wi;
using namespace coro;

IoTask::IoTask(IoScheduler& scheduler)
    : scheduler_(&scheduler)
    , coro_()
    , data_()
{
}

IoTask::~IoTask()
{
    // It's possible to be destroyed while co_await-ing on this
    // task, hence it should be removed from the `scheduler_`'s list.
    // 
    // Quoting Lewis Baker (https://github.com/lewissbaker):
    // "Awaitables that have been given responsibility for scheduling
    // resumption of a coroutine inside a `co_await` statement should
    // generally not need to worry about the coroutine being destroyed
    // out from under them"
    // 
    // Looks like it's bad idea to destroy coroutine while it's await-ing
    // something else
}

IoTask::IoTask(IoTask&& rhs) noexcept
    : scheduler_(rhs.scheduler_)
    , coro_(std::move(rhs.coro_))
    , data_(std::move(rhs.data_))
{
    rhs.scheduler_ = nullptr;
    rhs.coro_ = nullptr;
    rhs.data_ = PortEntry();
}

void IoTask::set(PortEntry data)
{
    assert(coro_);
    data_ = std::move(data);
    coro_.resume();
}

bool IoTask::await_ready() const noexcept
{
    // #TODO: we can check if there is any data available
    // and return it immediately
    return false;
}

PortEntry IoTask::await_resume() noexcept
{
    return std::move(data_);
}

void IoTask::await_suspend(std::coroutine_handle<> awaiter) noexcept
{
    assert(scheduler_);
    assert(!coro_);
    coro_ = awaiter;
    scheduler_->add(*this);
}
