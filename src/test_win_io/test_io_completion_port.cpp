#include <gtest/gtest.h>
#include <win_io/io_completion_port.h>

#include "file_utils.h"

#include <optional>

#include <cstdio>

using wi::IoCompletionPort;
using wi::PortEntry;

using namespace std::chrono_literals;

namespace
{
    class IoCompletionPortFileTest : public ::testing::Test
    {
    protected:
        virtual void SetUp() override
        {
            std::error_code ec;
            io_ = IoCompletionPort::make(ec);
            ASSERT_FALSE(ec);
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
            (void)::CloseHandle(file_);
        }

    protected:
        std::optional<IoCompletionPort> io_;
        OVERLAPPED ov_;
        HANDLE file_;
    };
} // namespace

TEST(IoCompletionPort, Creation_Does_Not_Fail)
{
    std::error_code ec;
    ASSERT_TRUE(IoCompletionPort::make(/*no threads limit*/ec).has_value());
    ASSERT_TRUE(IoCompletionPort::make(0/*use as much threads as CPUs*/, ec).has_value());
    ASSERT_TRUE(IoCompletionPort::make(20/*specific threads hint*/, ec).has_value());
}

TEST(IoCompletionPort, Blocking_Get_Success_After_Post)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(ec);
    ASSERT_FALSE(ec);
    const PortEntry send_data(1, 1, nullptr);
    port->post(send_data, ec);
    ASSERT_FALSE(ec);
    const auto receive_data = port->get(ec);
    ASSERT_FALSE(ec);
    ASSERT_EQ(send_data, receive_data);
}

TEST(IoCompletionPort, Blocking_Get_Success_After_Post_Many)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(ec);
    ASSERT_FALSE(ec);
    PortEntry to_send[2];
    to_send[0] = PortEntry(1, 1, nullptr);
    to_send[1] = PortEntry(2, 2, nullptr);
    for (const PortEntry& entry : to_send)
    {
        port->post(entry, ec);
        ASSERT_FALSE(ec);
    }
    PortEntry all_entries[3];
    const auto ready = port->get_many(all_entries, ec);
    ASSERT_FALSE(ec);
    ASSERT_EQ(std::size_t(2), ready.size());
    ASSERT_TRUE(std::equal(std::begin(to_send), std::end(to_send), std::begin(ready)));
}

TEST(IoCompletionPort, Has_No_Data_Until_Post)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(ec);
    ASSERT_FALSE(ec);
    auto receive_data = port->query(ec);
    ASSERT_TRUE(ec);
    ASSERT_EQ(std::nullopt, receive_data);
    
    const PortEntry send_data(1, 1, nullptr);
    port->post(send_data, ec);
    ASSERT_FALSE(ec);
    receive_data = port->query(ec);
    ASSERT_FALSE(ec);
    ASSERT_NE(std::nullopt, receive_data);
    ASSERT_EQ(send_data, *receive_data);
}

TEST(IoCompletionPort, Wait_Fails_Until_Post)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(ec);
    ASSERT_FALSE(ec);
    auto receive_data = port->wait_for(20ms, ec);
    ASSERT_TRUE(ec);
    ASSERT_EQ(std::nullopt, receive_data);

    const PortEntry send_data(1, 1, nullptr);
    port->post(send_data, ec);
    ASSERT_FALSE(ec);
    receive_data = port->wait_for(20ms, ec);
    ASSERT_FALSE(ec);
    ASSERT_NE(std::nullopt, receive_data);
    ASSERT_EQ(send_data, *receive_data);
}

TEST(IoCompletionPort, Associating_Invalid_Handle_Returns_Error)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(ec);
    ASSERT_FALSE(ec);
    port->associate_device(INVALID_HANDLE_VALUE, 1, ec);
    ASSERT_TRUE(ec);
}

TEST(IoCompletionPort, Associating_Invalid_Socket_Returns_Error)
{
    std::error_code ec;
    auto port = IoCompletionPort::make(ec);
    ASSERT_FALSE(ec);
    port->associate_socket(nullptr, 2, ec);
    ASSERT_TRUE(ec);
}

TEST_F(IoCompletionPortFileTest, Async_File_Write)
{
    std::error_code ec;
    io_->associate_device(file_, 1, ec);
    ASSERT_FALSE(ec)
        << "Associate with file failed: " << ec << ". "
        << "IoPort: " << io_->native_handle() << ". "
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
    const auto written = io_->get(ec);
    ASSERT_FALSE(ec);
    ASSERT_TRUE(written)
        << "Failed to wait for write end: " << ec;
    ASSERT_EQ(_countof(buffer), written->bytes_transferred);
    ASSERT_EQ(1u, written->completion_key);
    ASSERT_EQ(&ov_, written->overlapped);
}
