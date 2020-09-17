#include <gtest/gtest.h>

#include <win_io/detail/read_directory_changes.h>

#include "file_utils.h"

#include <memory>
#include <string>
#include <string_view>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>

#include <cstdio>
#include <cstring>

using namespace wi::detail;

using namespace std::chrono_literals;

namespace
{
    class DirectoryChangesTest : public ::testing::Test
    {
    protected:
        virtual void SetUp() override
        {
            dir_name_ = make_temporary_dir();

            std::memset(buffer_, 0, sizeof(buffer_));
        }

        virtual void TearDown() override
        {
            dir_changes_.reset();
            remove_temporary_dir();
        }

        void start_with_filters(WinDWORD filters)
        {
            dir_changes_ = std::make_unique<DirectoryChanges>(
                dir_name_.c_str(), buffer_
                , static_cast<WinDWORD>(sizeof(buffer_))
                , false, filters, io_port_);
            dir_changes_->start_watch();
        }

        std::wstring create_random_file()
        {
            const std::wstring name = utils::CreateTemporaryFile(dir_name_);
            created_files_.push_back(name);
            return name;
        }

        void delete_file(const std::wstring& name)
        {
            auto it = std::find(created_files_.begin(), created_files_.end(), name);
            ASSERT_NE(created_files_.end(), it)
                << "Trying to delete not created by current test file: " << name;
            created_files_.erase(it);
            ASSERT_TRUE(::DeleteFileW(name.c_str()))
                << "Failed to delete file: " << name
                << ". Error: " << ::GetLastError();
        }

    private:
        std::wstring make_temporary_dir()
        {
            return utils::CreateTemporaryDir();
        }

        void remove_temporary_dir()
        {
            for (const auto& file : created_files_)
            {
                ASSERT_TRUE(::DeleteFileW(file.c_str()))
                    << "Failed to delete file: " << file
                    << ". Error: " << ::GetLastError();
            }
            ASSERT_TRUE(::RemoveDirectoryW(dir_name_.c_str()))
                << "Failed to delete directory: " << dir_name_
                << ". Error: " << ::GetLastError();
        }

    protected:
        IoCompletionPort io_port_;
        DWORD buffer_[128];
        std::unique_ptr<DirectoryChanges> dir_changes_;
        std::wstring dir_name_;
        std::vector<std::wstring> created_files_;
    };

    bool EndsWith(const std::wstring& str, const std::wstring_view& end)
    {
#if (1)
        // Wrapping `str` into `wstring_view` since std version
        // of wstring knows nothing about non-standard view
        const auto pos = std::wstring_view(str).rfind(end);
#else
        const auto pos = str.rfind(end);
#endif
        if (pos == str.npos)
        {
            return false;
        }
        return (str.size() == (pos + end.size()));
    }

} // namespace

TEST_F(DirectoryChangesTest, IOPort_Receives_File_Added_Event_After_File_Creation)
{
    start_with_filters(FILE_NOTIFY_CHANGE_FILE_NAME);
    const auto file = create_random_file();
    const auto results = dir_changes_->wait_for(10ms);
    const auto changes = results.directory_changes();
    ASSERT_TRUE(changes);

    const auto count = std::distance(changes->cbegin(), changes->cend());
    ASSERT_EQ(1u, count);

    const auto info = *(changes->begin());
    ASSERT_EQ(static_cast<DWORD>(FILE_ACTION_ADDED), info.action);
    ASSERT_TRUE(EndsWith(file, info.name));
}

TEST_F(DirectoryChangesTest, Wait_On_Dir_Change_Can_Return_Other_Port_Data)
{
    start_with_filters(FILE_NOTIFY_CHANGE_FILE_NAME);

    io_port_.post(PortData(10, 1, nullptr));
    std::error_code ec;
    auto results = dir_changes_->wait_for(10ms, ec);
    ASSERT_FALSE(ec);
    // Wait resulted to getting PortData
    const auto port_data = results.port_changes();
    ASSERT_TRUE(port_data);
    ASSERT_EQ(WinDWORD(10), port_data->value);
    ASSERT_EQ(WinULONG_PTR(1), port_data->key);
    ASSERT_EQ(nullptr, port_data->ptr);

    // Now, create & handle directory event
    const auto file = create_random_file();
    results = dir_changes_->wait_for(10ms);
    const auto changes = results.directory_changes();
    ASSERT_TRUE(changes);

    const auto count = std::distance(changes->cbegin(), changes->cend());
    ASSERT_EQ(1u, count);

    const auto info = *(changes->begin());
    ASSERT_EQ(static_cast<DWORD>(FILE_ACTION_ADDED), info.action);
    ASSERT_TRUE(EndsWith(file, info.name));
}

TEST_F(DirectoryChangesTest, Start_Needs_To_Be_Called_After_Successfull_Wait)
{
    start_with_filters(FILE_NOTIFY_CHANGE_FILE_NAME);
    const auto file = create_random_file();

    {
        auto results = dir_changes_->query();
        const auto changes = results.directory_changes();
        ASSERT_TRUE(changes);
        const auto count = std::distance(changes->cbegin(), changes->cend());
        ASSERT_EQ(1u, count);

        auto change = changes->begin();
        ASSERT_NE(DirectoryChangesIterator(), change);
        ASSERT_EQ(static_cast<DWORD>(FILE_ACTION_ADDED), change->action);
        ASSERT_TRUE(EndsWith(file, change->name));

        auto end = ++change;
        ASSERT_EQ(DirectoryChangesIterator(), end);
    }

    dir_changes_->start_watch();
    delete_file(file);

    {
        auto results = dir_changes_->query();
        const auto changes = results.directory_changes();
        ASSERT_TRUE(changes);
        const auto count = std::distance(changes->cbegin(), changes->cend());
        ASSERT_EQ(1u, count);

        auto change = changes->begin();
        ASSERT_NE(DirectoryChangesIterator(), change);
        ASSERT_EQ(static_cast<DWORD>(FILE_ACTION_REMOVED), change->action);
        ASSERT_TRUE(EndsWith(file, change->name));

        auto end = ++change;
        ASSERT_EQ(DirectoryChangesIterator(), end);
    }
}

namespace
{
    // Deep copy of DirectoryChange
    struct Change
    {
        WinDWORD action;
        std::wstring name;
        
        Change(WinDWORD change_action, std::wstring change_name)
            : action(change_action)
            , name(std::move(change_name))
        {
        }

        Change(const DirectoryChange& change)
            : Change(change.action, std::wstring(change.name))
        {
        }
    };

    bool operator==(const Change& lhs, const Change& rhs)
    {
        return (lhs.action == rhs.action)
            && (lhs.name == rhs.name);
    }

    std::wstring GetPathFileName(const std::wstring& str)
    {
        // Ignore any error handling
        return str.substr(str.rfind('\\') + 1);
    }

    bool AreChangesEqual(const std::vector<Change>& v1, const std::vector<Change>& v2)
    {
        return std::is_permutation(v1.begin(), v1.end(), v2.begin(), v2.end());
    }

} // namespace

TEST_F(DirectoryChangesTest, Waiting_From_Multiple_Threads)
{
    std::atomic_bool running(true);
    std::vector<std::thread> workers;
    std::mutex lock;
    std::vector<Change> detected_changes;
    std::vector<Change> created_changes;

    start_with_filters(FILE_NOTIFY_CHANGE_FILE_NAME);

    auto waiter = [&]
    {
        std::error_code ec;
        while (running)
        {
            auto results = dir_changes_->wait_for(10ms, ec);
            if (ec)
            {
                continue;
            }

            const auto changes = results.directory_changes();
            ASSERT_TRUE(changes);
            {
                std::lock_guard<std::mutex> _(lock);
                for (auto change : *changes)
                {
                    detected_changes.emplace_back(change);
                }
            }

            dir_changes_->start_watch();
        }
    };

    workers.emplace_back(waiter);
    workers.emplace_back(waiter);
    workers.emplace_back(waiter);

    // Generate some changes
    for (int i = 0; i < 10; ++i)
    {
        const auto file = create_random_file();
        created_changes.emplace_back(FILE_ACTION_ADDED, GetPathFileName(file));
        delete_file(file);
        created_changes.emplace_back(FILE_ACTION_REMOVED, GetPathFileName(file));
    }

    // Give some time for threads to process all events
    std::this_thread::sleep_for(20ms);

    running = false;
    for (auto& worker : workers)
    {
        worker.join();
    }

    ASSERT_TRUE(AreChangesEqual(created_changes, detected_changes));
}

TEST_F(DirectoryChangesTest, Detects_Buffer_Overflow_With_No_Errors_Flag_Set)
{
    // Amount of space one change takes in the buffer
    // with "zero" file name length.
    const std::size_t k_base_change_size = sizeof(FILE_NOTIFY_INFORMATION) - sizeof(DWORD);
    const std::size_t k_iterations_to_fill_buffer =
        (sizeof(buffer_) / k_base_change_size);
    static_assert(k_iterations_to_fill_buffer != 0, "");
    
    start_with_filters(FILE_NOTIFY_CHANGE_FILE_NAME);

    for (size_t i = 0; i < k_iterations_to_fill_buffer; ++i)
    {
        const auto file = create_random_file();
        delete_file(file);
    }

    std::error_code ec;
    bool has_buffer_overflow = false;
    for (int i = 0; i < 5/*try few times*/; ++i)
    {
        while (auto data = io_port_.query(ec))
        {
            ASSERT_TRUE(dir_changes_->is_directory_change(*data));
            if (dir_changes_->has_buffer_overflow(*data))
            {
                has_buffer_overflow = true;
                // No error detected
                ASSERT_FALSE(ec);
                // No actual changes
                DirectoryChangesRange changes(dir_changes_->buffer(), *data);
                const std::size_t count = static_cast<std::size_t>(
                    std::distance(changes.begin(), changes.end()));
                ASSERT_EQ(0u, count);
                break;
            }
            else
            {
                dir_changes_->start_watch();
            }
        }
    }
    ASSERT_TRUE(has_buffer_overflow);
}
