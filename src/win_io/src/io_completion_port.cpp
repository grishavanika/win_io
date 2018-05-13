#include <win_io/detail/io_completion_port.h>
#include <win_io/detail/last_error_utils.h>
#include <win_io/detail/io_completion_port_errors.h>

#include <limits>

#include <Windows.h>

using namespace wi;
using namespace detail;

IoCompletionPort::IoCompletionPort()
	// Do not limit number of threads by default
	: IoCompletionPort((std::numeric_limits<std::uint32_t>::max)())
{
}

IoCompletionPort::IoCompletionPort(std::uint32_t concurrent_threads_hint)
	: io_port_(nullptr)
{
	io_port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE
		, nullptr // Create new one
		, 0	// No completion key yet
		, concurrent_threads_hint);

	if (!io_port_)
	{
		throw_last_error<IoCompletionPortError>("[Io] ::CreateIoCompletionPort()");
	}
}

IoCompletionPort::~IoCompletionPort()
{
	const bool ok = ::CloseHandle(io_port_);
	(void)ok;
}
