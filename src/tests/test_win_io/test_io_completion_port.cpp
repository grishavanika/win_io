#include <gtest/gtest.h>
#include <win_io/detail/io_completion_port.h>

using wi::detail::IoCompletionPort;

TEST(IoCompletionPort, Creation_Does_Not_Throw)
{
	ASSERT_NO_THROW(IoCompletionPort(/*no threads limit*/));
	ASSERT_NO_THROW(IoCompletionPort(0/*use as much thread as CPUs*/));
	ASSERT_NO_THROW(IoCompletionPort(20/*specific threads hint*/));
}

