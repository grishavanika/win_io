#include <gtest/gtest.h>
#include <win_io/detail/io_completion_port.h>

#include "file_utils.h"

#include <nonstd/optional.hpp>

#include <cstdio>

using wi::detail::IoCompletionPort;
using wi::detail::PortData;

using namespace std::chrono_literals;

namespace
{
	class IoCompletionPortFileTest : public ::testing::Test
	{
	protected:
		virtual void SetUp() override
		{
			const std::wstring temp_name = utils::CreateTemporaryFile();
			file_ = ::CreateFileW(temp_name.c_str()
				, GENERIC_WRITE
				, FILE_SHARE_READ
				, nullptr
				, OPEN_EXISTING
				, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_DELETE_ON_CLOSE
				, nullptr);
			ASSERT_NE(INVALID_HANDLE_VALUE, file_)
				<< "Failed to open temp file for async. write. "
				<< "File name: " << temp_name;
			
			ov_ = OVERLAPPED();
		}

		virtual void TearDown() override
		{
			::CloseHandle(file_);
		}

	protected:
		IoCompletionPort io_;
		OVERLAPPED ov_;
		HANDLE file_;
	};
} // namespace

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
	ASSERT_EQ(nonstd::nullopt, receive_data);
	
	const PortData send_data(1, 1, nullptr);
	port.post(send_data);
	receive_data = port.query(ec);
	ASSERT_FALSE(ec);
	ASSERT_NE(nonstd::nullopt, receive_data);
	ASSERT_EQ(send_data, *receive_data);
}

TEST(IoCompletionPort, Wait_Fails_Until_Post)
{
	IoCompletionPort port;
	std::error_code ec;
	auto receive_data = port.wait_for(20ms, ec);
	ASSERT_TRUE(ec);
	ASSERT_EQ(nonstd::nullopt, receive_data);

	const PortData send_data(1, 1, nullptr);
	port.post(send_data);
	receive_data = port.wait_for(20ms, ec);
	ASSERT_FALSE(ec);
	ASSERT_NE(nonstd::nullopt, receive_data);
	ASSERT_EQ(send_data, *receive_data);
}

TEST(IoCompletionPort, Associating_Invalid_Handle_Returns_Error)
{
	IoCompletionPort port;
	std::error_code ec;
	port.associate_device(INVALID_HANDLE_VALUE, 1, ec);
	ASSERT_TRUE(ec);
}

TEST(IoCompletionPort, Associating_Invalid_Socket_Returns_Error)
{
	IoCompletionPort port;
	std::error_code ec;
	port.associate_socket(nullptr, 2, ec);
	ASSERT_TRUE(ec);
}

TEST_F(IoCompletionPortFileTest, Async_File_Write)
{
	std::error_code ec;
	io_.associate_device(file_, 1, ec);
	ASSERT_FALSE(ec)
		<< "Associate with file failed: " << ec << ". "
		<< "IoPort: " << io_.native_handle() << ". "
		<< "File: " << file_;

	char buffer[1 * 1024] = "Some text with zeroes at the end";
	ov_.Offset = 0;
	const BOOL status = ::WriteFile(file_, buffer, _countof(buffer), nullptr, &ov_);
	const bool async_start = !status && (::GetLastError() == ERROR_IO_PENDING);
	if (!async_start)
	{
		ASSERT_TRUE(status) << "Async. write to file failed: " << ::GetLastError();
		return;
	}
	const auto written = io_.get(ec);
	ASSERT_TRUE(written)
		<< "Failed to wait for write end: " << ec;
	ASSERT_EQ(_countof(buffer), written->value);
	ASSERT_EQ(1u, written->key);
	ASSERT_EQ(&ov_, written->ptr);
}
