#include <gtest/gtest.h>
#include <win_io/detail/io_completion_port.h>

using wi::detail::IoCompletionPort;
using wi::detail::PortData;

using namespace std::chrono_literals;

TEST(IoCompletionPort, Creation_Does_Not_Throw)
{
	ASSERT_NO_THROW(IoCompletionPort(/*no threads limit*/));
	ASSERT_NO_THROW(IoCompletionPort(0/*use as much threads as CPUs*/));
	ASSERT_NO_THROW(IoCompletionPort(20/*specific threads hint*/));
}

TEST(IoCompletionPort, Blocking_Get_Success_After_Post)
{
	IoCompletionPort port;
	const PortData send_data(1, 1, nullptr);
	port.post(send_data);
	const auto receive_data = port.get();
	ASSERT_EQ(send_data, receive_data);
}

TEST(IoCompletionPort, Has_No_Data_Until_Post)
{
	IoCompletionPort port;
	std::error_code ec;
	auto receive_data = port.query(ec);
	ASSERT_TRUE(ec);
	ASSERT_EQ(std::nullopt, receive_data);
	
	const PortData send_data(1, 1, nullptr);
	port.post(send_data);
	receive_data = port.query(ec);
	ASSERT_FALSE(ec);
	ASSERT_NE(std::nullopt, receive_data);
	ASSERT_EQ(send_data, *receive_data);
}

TEST(IoCompletionPort, Wait_Fails_Until_Post)
{
	IoCompletionPort port;
	std::error_code ec;
	auto receive_data = port.wait_for(20ms, ec);
	ASSERT_TRUE(ec);
	ASSERT_EQ(std::nullopt, receive_data);

	const PortData send_data(1, 1, nullptr);
	port.post(send_data);
	receive_data = port.wait_for(20ms, ec);
	ASSERT_FALSE(ec);
	ASSERT_NE(std::nullopt, receive_data);
	ASSERT_EQ(send_data, *receive_data);
}

