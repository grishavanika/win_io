#pragma once
#include <win_io/detail/io_completion_port.h>

namespace wi
{
	namespace detail
	{

		// Low-level wrapper around ::ReadDirectoryChangesW()
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

			WinHANDLE directory() const;

		private:
			WinHANDLE open_directory(const wchar_t* directory_name);
			void start_watch();

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
