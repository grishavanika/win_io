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
	if (scheduler_)
	{
		// #TODO: think about this. What can be done ?
		// We can die in (probably) in-progress state
		scheduler_->remove(*this);
	}
}

IoTask::IoTask(IoTask&& rhs) noexcept
	: scheduler_(rhs.scheduler_)
	, coro_(std::move(rhs.coro_))
	, data_(std::move(rhs.data_))
{
	rhs.scheduler_ = nullptr;
	rhs.coro_ = nullptr;
	rhs.data_ = PortData();
}

void IoTask::set(PortData data)
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

PortData IoTask::await_resume() noexcept
{
	return std::move(data_);
}

void IoTask::await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept
{
	assert(scheduler_);
	assert(!coro_);
	coro_ = awaiter;
	scheduler_->add(*this);
}
