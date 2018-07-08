#include <win_io_coro/io_coro_scheduler.h>

#include <cassert>

using namespace wi;
using namespace coro;

IoScheduler::IoScheduler(IoCompletionPort& io_port)
	: io_port_(io_port)
{
}

IoScheduler::~IoScheduler()
{
	assert(tasks_.is_empty() &&
		"Coroutine tasks still running while destroying IoScheduler. "
		"Probably access to deleted object will happen");
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

	IoTask* task = nullptr;
	if (!tasks_.pop(task))
	{
		// We have no any waiting task. Put the data back to the I/O Port
		io_port_.post(*data, ec);
		// #TODO: think about proper error handling
		assert(!ec && "Failed to put data back to the I/O Port: "
			"data lost happened");
		return 0;
	}

	task->set(std::move(*data));
	return 1;
}

IoTask IoScheduler::get()
{
	return IoTask(*this);
}

void IoScheduler::add(IoTask& task)
{
	tasks_.push(&task);
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
