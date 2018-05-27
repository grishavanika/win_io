#pragma once
#include <win_io/detail/io_completion_port.h>
#include <win_io/detail/directory_changes_range.h>
#include <win_io/detail/read_directory_changes_errors.h>

#include <variant>

namespace wi
{
	namespace detail
	{
		// `DirectoryChanges` helper is thin adapter around `IoCompletionPort`
		// that does not own the port. As a result, waiting for directory change
		// can lead to getting some other-not-directory-relative 
		// IoCompletionPort's data since clients of the port can use it in
		// multiple ways (posting custom data, waiting for other changes and so on).
		// Hence, it's a client responsibility to handle wait results.
		// 
		// DirectoryChangesResults results = dir_changes.get();
		// if (results.directory_changes()) { /*process directory changes*/ }
		// else if (results.port_changes()) { /*some other PortData */ }
		class DirectoryChangesResults
		{
		public:
			explicit DirectoryChangesResults();
			explicit DirectoryChangesResults(DirectoryChangesRange dir_changes);
			explicit DirectoryChangesResults(PortData port_changes);

			bool has_changes() const;

			DirectoryChangesRange* directory_changes();
			const DirectoryChangesRange* directory_changes() const;
			
			PortData* port_changes();
			const PortData* port_changes() const;

		private:
			std::variant<DirectoryChangesRange, PortData> data_;
		};

		// Low-level wrapper around `::ReadDirectoryChangesW()`
		// with IOCompletionPort usage.
		// Wait on the data can be done from multiple threads.
		// General use case can look like:
		// 
		// dir_changes_.start_watch();
		// while (is_waiting()) {
		//		DirectoryChangesResults results = dir_changes.get();
		//		process_changes(results);
		//		dir_changes_.start_watch();
		// }
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

			DirectoryChangesResults get(std::error_code& ec);
			DirectoryChangesResults get();

			DirectoryChangesResults query(std::error_code& ec);
			DirectoryChangesResults query();
			
			template<typename Rep, typename Period>
			DirectoryChangesResults wait_for(std::chrono::duration<Rep, Period> time
				, std::error_code& ec);

			template<typename Rep, typename Period>
			DirectoryChangesResults wait_for(std::chrono::duration<Rep, Period> time);

		private:
			WinHANDLE open_directory(const wchar_t* directory_name);
			DirectoryChangesResults wait_impl(WinDWORD milliseconds
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
		DirectoryChangesResults DirectoryChanges::wait_for(
			std::chrono::duration<Rep, Period> time, std::error_code& ec)
		{
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time);
			return wait_impl(static_cast<WinDWORD>(ms.count()), ec);
		}

		template<typename Rep, typename Period>
		DirectoryChangesResults DirectoryChanges::wait_for(
			std::chrono::duration<Rep, Period> time)
		{
			std::error_code ec;
			auto data = wait_for(std::move(time), ec);
			throw_if_error<DirectoryChangesError>("[Dc] failed to wait for data", ec);
			return data;
		}

		inline DirectoryChangesResults::DirectoryChangesResults()
			: data_()
		{
		}

		inline DirectoryChangesResults::DirectoryChangesResults(DirectoryChangesRange dir_changes)
			: data_(std::move(dir_changes))
		{
		}

		inline DirectoryChangesResults::DirectoryChangesResults(PortData port_changes)
			: data_(std::move(port_changes))
		{
		}

		inline bool DirectoryChangesResults::has_changes() const
		{
			return (data_.index() != std::variant_npos);
		}

		inline DirectoryChangesRange* DirectoryChangesResults::directory_changes()
		{
			return std::get_if<DirectoryChangesRange>(&data_);
		}

		inline const DirectoryChangesRange* DirectoryChangesResults::directory_changes() const
		{
			return std::get_if<DirectoryChangesRange>(&data_);
		}

		inline PortData* DirectoryChangesResults::port_changes()
		{
			return std::get_if<PortData>(&data_);
		}

		inline const PortData* DirectoryChangesResults::port_changes() const
		{
			return std::get_if<PortData>(&data_);
		}

	} // namespace detail
} // namespace wi
