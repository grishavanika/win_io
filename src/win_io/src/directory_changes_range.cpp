#include <win_io/detail/directory_changes_range.h>

#include <cassert>

#include <Windows.h>

using namespace wi;
using namespace detail;

namespace
{

	const FILE_NOTIFY_INFORMATION& GetInfo(const void* buffer)
	{
		assert(buffer && "Trying to dereference end() iterator");
		return *static_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
	}

	const void* MoveToNext(const void* buffer)
	{
		const auto& info = GetInfo(buffer);
		const bool has_more = (info.NextEntryOffset != 0);
		return has_more
			? static_cast<const std::uint8_t*>(buffer) + info.NextEntryOffset
			: nullptr;
	}

	DirectoryChange GetValue(const void* buffer)
	{
		const auto& info = GetInfo(buffer);
		const std::size_t name_length = (static_cast<std::size_t>(info.FileNameLength) / sizeof(wchar_t));
		DirectoryChange change;
		change.action = info.Action;
		change.name = nonstd::wstring_view(info.FileName, name_length);
		return change;
	}
} // namespace

/*explicit*/ DirectoryChangesIterator::DirectoryChangesIterator()
	: current_(nullptr)
{
}

/*explicit*/ DirectoryChangesIterator::DirectoryChangesIterator(const void* buffer)
	: current_(buffer)
{
}

const DirectoryChange DirectoryChangesIterator::operator*()
{
	value_ = GetValue(current_);
	return value_;
}

const DirectoryChange* DirectoryChangesIterator::operator->()
{
	value_ = GetValue(current_);
	return &value_;
}

DirectoryChangesIterator DirectoryChangesIterator::operator++()
{
	current_ = MoveToNext(current_);
	return DirectoryChangesIterator(current_);
}

DirectoryChangesIterator DirectoryChangesIterator::operator++(int)
{
	return DirectoryChangesIterator(MoveToNext(current_));
}

namespace wi
{
	namespace detail
	{

		bool operator==(const DirectoryChangesIterator& lhs
			, const DirectoryChangesIterator& rhs)
		{
			return (lhs.current_ == rhs.current_);
		}

		bool operator!=(const DirectoryChangesIterator& lhs
			, const DirectoryChangesIterator& rhs)
		{
			return !(lhs == rhs);
		}

	} // namespace detail
} // namespace wi
