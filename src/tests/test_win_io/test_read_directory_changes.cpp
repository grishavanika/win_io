#include <gtest/gtest.h>
#include <win_io/detail/read_directory_changes.h>

#include <Windows.h>

#include <memory>
#include <string>
#include <fstream>

#include <cstdio>
#include <cstring>

using wi::detail::IoCompletionPort;
using wi::detail::DirectoryChanges;
using wi::detail::DirectoryChangesRange;
using wi::detail::DirectoryChange;
using wi::detail::WinDWORD;

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
#if (_MSC_VER)
			const std::wstring name = dir_name_ + L"/" + L"test.bin";
			std::ofstream file(name);
			file.close();
			created_files_.push_back(name);
			return std::wstring(name);
#else
			[&]
			{
				ASSERT_FALSE(true) << "Create file for GCC is not implemented yet";
			}();
			return std::wstring();
#endif
		}

	private:
		std::wstring make_temporary_dir()
		{
			const wchar_t* name = L"temp";
			[&]
			{
				ASSERT_TRUE(::CreateDirectoryW(name, nullptr))
					<< "Failed to create directory: " << ::GetLastError();
			}();
			return std::wstring(name);
		}

		void remove_temporary_dir()
		{
			for (const auto& file : created_files_)
			{
				(void)::DeleteFileW(file.c_str());
			}
			(void)::RemoveDirectoryW(dir_name_.c_str());
		}

	protected:
		IoCompletionPort io_port_;
		DWORD buffer_[128];
		std::unique_ptr<DirectoryChanges> dir_changes_;
		std::wstring dir_name_;
		std::vector<std::wstring> created_files_;
	};
} // namespace

TEST_F(DirectoryChangesTest, IOPort_Receives_File_Added_Event_After_File_Creation)
{
	start_with_filters(FILE_NOTIFY_CHANGE_FILE_NAME);
	const auto file = create_random_file();
	const auto event = io_port_.wait_for(1s);
	ASSERT_TRUE(event);

	const DirectoryChangesRange changes(buffer_);
	const auto count = std::distance(changes.cbegin(), changes.cend());
	ASSERT_EQ(1u, count);

	const auto info = *changes.begin();
	ASSERT_EQ(static_cast<DWORD>(FILE_ACTION_ADDED), info.action);
#if defined(_MSC_VER)
	const auto file_name_pos = file.find(info.name);
	ASSERT_NE(file.npos, file_name_pos);
	ASSERT_EQ(file.size(), file_name_pos + info.name.size());
#else
	// GCC does not support std::string.find(std::string_view) yet
#endif
}
