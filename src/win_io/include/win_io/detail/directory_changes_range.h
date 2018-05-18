#pragma once
#include <win_io/detail/win_types.h>

#if (__has_include(<string_view>))
#  include <string_view>
#else
#  include <experimental/string_view>
namespace std
{
	using wstring_view = experimental::wstring_view;
} // namespace std
#endif

#include <iterator>

namespace wi
{
	namespace detail
	{
		struct DirectoryChange
		{
			WinDWORD action = 0;
			// Warning: not-null terminated file or folder name that caused `action`
			std::wstring_view name;
		};

		class DirectoryChangesIterator;

		// Parser of `FILE_NOTIFY_INFORMATION` from void* buffer
		// that provides read-only, iterator-like interface
		class DirectoryChangesRange
		{
		public:
			using iterator = DirectoryChangesIterator;
			using const_iterator = DirectoryChangesIterator;

			// Requires `buffer` to be filled with valid state
			// after one succesfull call to `::ReadDirectoryChangesW()`.
			// (`buffer` is DWORD-aligned, variable length array of
			// `FILE_NOTIFY_INFORMATION` structs)
			explicit DirectoryChangesRange(const void* buffer);

			iterator begin();
			const_iterator begin() const;
			const_iterator cbegin();
			const_iterator cbegin() const;

			iterator end();
			const_iterator end() const;
			const_iterator cend();
			const_iterator cend() const;

		private:
			const void* const buffer_;
		};

		// Models read-only `ForwardIterator`
		class DirectoryChangesIterator
		{
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = const DirectoryChange;
			using reference = const DirectoryChange;
			using pointer = const DirectoryChange*;
			using difference_type = std::ptrdiff_t;

			explicit DirectoryChangesIterator();
			explicit DirectoryChangesIterator(const void* buffer);

			DirectoryChangesIterator operator++();
			DirectoryChangesIterator operator++(int);

			// Warning: do not save pointer from temporary iterator.
			// No real reference or pointer to persistent memory returned.
			// All variables are created on the fly
			const DirectoryChange operator*();
			const DirectoryChange* operator->();
			
			friend bool operator==(const DirectoryChangesIterator& lhs
				, const DirectoryChangesIterator& rhs);
			friend bool operator!=(const DirectoryChangesIterator& lhs
				, const DirectoryChangesIterator& rhs);

		private:
			const void* current_;
			// Needed for proper implementation of operator->()
			// (that requires to return pointer)
			DirectoryChange value_;
		};

		bool operator==(const DirectoryChangesIterator& lhs
			, const DirectoryChangesIterator& rhs);
		bool operator!=(const DirectoryChangesIterator& lhs
			, const DirectoryChangesIterator& rhs);

	} // namespace detail
} // namespace wi

namespace wi
{
	namespace detail
	{

		/*explicit*/ inline DirectoryChangesRange::DirectoryChangesRange(const void* buffer)
			: buffer_(buffer)
		{
		}

		inline DirectoryChangesRange::iterator DirectoryChangesRange::begin()
		{
			return iterator(buffer_);
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::begin() const
		{
			return const_iterator(buffer_);
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::cbegin()
		{
			return const_iterator(buffer_);
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::cbegin() const
		{
			return const_iterator(buffer_);
		}

		inline DirectoryChangesRange::iterator DirectoryChangesRange::end()
		{
			return iterator();
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::end() const
		{
			return const_iterator();
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::cend()
		{
			return const_iterator();
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::cend() const
		{
			return const_iterator();
		}

	} // namespace detail
} // namespace wi


