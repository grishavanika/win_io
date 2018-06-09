#include <win_io_coro/io_coro_scheduler.h>

#include <cassert>

using namespace wi;
using namespace coro;

IoScheduler::IoScheduler(detail::IoCompletionPort& io_port)
	: io_port_(io_port)
{
}

std::size_t IoScheduler::poll_one()
{
	std::error_code ec;
	auto data = io_port_.query(ec);
	// #TODO: read Asio documentation to understand where error code
	// is propagated for poll_one() interface
	(void)ec;
	if (!data)
	{
		return 0;
	}

	if (task_)
	{
		// #TODO: pop task from the queue
		IoTask* task = task_;
		task_ = nullptr;

		task->set(std::move(*data));
		return 1;
	}
	return 0;
}

IoTask IoScheduler::get()
{
	return IoTask(*this);
}

void IoScheduler::add(IoTask& task)
{
	// #TODO: maintain concurrent list of tasks
	assert(!task_);
	task_ = &task;
}

std::size_t IoScheduler::poll()
{
	// #TODO: add stop() mechanizm
	std::size_t tasks = 0;
	while (poll_one() == 1)
	{
		++tasks;
	}
	return tasks;
}

std::size_t IoScheduler::run()
{
	assert(!"Not implemented yet");
	return 0;
}

std::size_t IoScheduler::run_one()
{
	assert(!"Not implemented yet");
	return 0;
}
