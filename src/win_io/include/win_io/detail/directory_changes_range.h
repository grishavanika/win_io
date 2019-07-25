#pragma once
#include <win_io/detail/win_types.h>
#include <win_io/detail/cpp17_integration.h>

#include <iterator>

#include <cassert>

namespace wi
{
	namespace detail
	{
		struct DirectoryChange
		{
			WinDWORD action;
			// Warning: not-null terminated file or folder name that caused `action`
			nonstd::wstring_view name;

			DirectoryChange(WinDWORD change_action = 0
				, nonstd::wstring_view change_name = {});
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
			explicit DirectoryChangesRange(const void* buffer, std::size_t size);
			explicit DirectoryChangesRange();

			bool has_any() const;

			iterator begin();
			const_iterator begin() const;
			const_iterator cbegin();
			const_iterator cbegin() const;

			iterator end();
			const_iterator end() const;
			const_iterator cend();
			const_iterator cend() const;

		private:
			const void* buffer_;
			std::size_t size_;
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
			explicit DirectoryChangesIterator(const void* buffer, const std::size_t max_size);

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
			void move_to_next();
			std::size_t available_size() const;

		private:
			const void* current_;
			// Needed for proper implementation of operator->()
			// (that requires to return pointer)
			DirectoryChange value_;

			std::size_t consumed_size_;
            std::size_t max_size_;
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

		inline DirectoryChange::DirectoryChange(WinDWORD change_action /*= 0*/
			, nonstd::wstring_view change_name /*= {}*/)
			: action(change_action)
			, name(std::move(change_name))
		{
		}

		/*explicit*/ inline DirectoryChangesRange::DirectoryChangesRange(
			const void* buffer, const std::size_t size)
			: buffer_(buffer)
			, size_(size)
		{
		}

		/*explicit*/ inline DirectoryChangesRange::DirectoryChangesRange()
			: DirectoryChangesRange(nullptr, 0)
		{
		}

		inline bool DirectoryChangesRange::has_any() const
		{
			return (size_ != 0) && (buffer_ != nullptr);
		}

		inline DirectoryChangesRange::iterator DirectoryChangesRange::begin()
		{
			return iterator(buffer_, size_);
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::begin() const
		{
			return const_iterator(buffer_, size_);
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::cbegin()
		{
			return const_iterator(buffer_, size_);
		}

		inline DirectoryChangesRange::const_iterator DirectoryChangesRange::cbegin() const
		{
			return const_iterator(buffer_, size_);
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


