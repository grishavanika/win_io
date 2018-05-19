#pragma once
#include <win_io/detail/io_completion_port.h>
#include <win_io/detail/directory_changes_range.h>
#include <win_io/detail/read_directory_changes_errors.h>

#include <variant>

namespace wi
{
	namespace detail
	{
		// #TODO: make nice struct with properly names check/query methods
		using DirectoryChangesWait = std::variant<DirectoryChangesRange, PortData>;

		// Low-level wrapper around `::ReadDirectoryChangesW()`
		// with IOCompletionPort usage
		class DirectoryChanges
		{
		public:
			// #TODO: use span instead of raw pinter & size
			DirectoryChanges(WinHANDLE directory, void* buffer, WinDWORD length
				, bool watch_sub_tree, WinDWORD notify_filter
				, IoCompletionPort& io_port
				, WinULONG_PTR dir_key = 1);
			DirectoryChanges(const wchar_t* directory_name
				, void* buffer, WinDWORD length
				, bool watch_sub_tree, WinDWORD notify_filter
				, IoCompletionPort& io_port
				, WinULONG_PTR dir_key = 1);
			~DirectoryChanges();

			WinHANDLE directory_handle() const;

			// Usefull when single I/O completion port is used for
			// tracking multiple directories changes.
			// You will need to wait for I/O event and then check
			// from which directory it coming.
			// Once you will need to process data,
			// it's possible to construct `DirectoryChangesRange` from `buffer()`
			bool is_directory_change(const PortData& data) const;

			const void* buffer() const;

			// Call after each successfull wait for event
			// (or after `DirectoryChanges` instance creation).
			// Be sure to call only after processing data stored
			// in the `buffer` since system can write into it
			// while you are reading from
			void start_watch(std::error_code& ec);
			void start_watch();

			DirectoryChangesWait get(std::error_code& ec);
			DirectoryChangesWait get();

			DirectoryChangesWait query(std::error_code& ec);
			DirectoryChangesWait query();
			
			template<typename Rep, typename Period>
			DirectoryChangesWait wait_for(std::chrono::duration<Rep, Period> time
				, std::error_code& ec);

			template<typename Rep, typename Period>
			DirectoryChangesWait wait_for(std::chrono::duration<Rep, Period> time);

		private:
			WinHANDLE open_directory(const wchar_t* directory_name);
			DirectoryChangesWait wait_impl(WinDWORD milliseconds
				, std::error_code& ec);

		private:
			IoCompletionPort& io_port_;
			WinHANDLE directory_;
			void* buffer_;
			WinULONG_PTR dir_key_;
			WinOVERLAPPEDBuffer ov_buffer_;
			WinDWORD length_;
			WinDWORD notify_filter_;
			bool owns_directory_;
			bool watch_sub_tree_;
		};

	} // namespace detail
} // namespace wi

namespace wi
{
	namespace detail
	{

		template<typename Rep, typename Period>
		DirectoryChangesWait DirectoryChanges::wait_for(
			std::chrono::duration<Rep, Period> time, std::error_code& ec)
		{
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time);
			return wait_impl(static_cast<WinDWORD>(ms.count()), ec);
		}

		template<typename Rep, typename Period>
		DirectoryChangesWait DirectoryChanges::wait_for(
			std::chrono::duration<Rep, Period> time)
		{
			std::error_code ec;
			auto data = wait_for(std::move(time), ec);
			throw_if_error<DirectoryChangesError>("[Dc] failed to wait for data", ec);
			return data;
		}

	} // namespace detail
} // namespace wi
