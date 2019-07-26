#include <win_io/detail/directory_changes_range.h>

#include <Windows.h>

using namespace wi;
using namespace detail;

namespace
{

	const FILE_NOTIFY_INFORMATION& GetInfo(const void* buffer)
	{
		assert(buffer);
		return *static_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
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

	std::size_t GetInfoSize(const FILE_NOTIFY_INFORMATION& fi)
	{
		std::size_t size = 0;
		size += sizeof(FILE_NOTIFY_INFORMATION) - sizeof(DWORD);
		size += fi.FileNameLength;
		return size;
	}

	std::size_t GetInfoSize(const void* buffer)
	{
		return GetInfoSize(GetInfo(buffer));
	}

} // namespace

/*explicit*/ DirectoryChangesIterator::DirectoryChangesIterator()
	: current_(nullptr)
	, value_()
	, consumed_size_(0)
	, max_size_(0)
{
}

/*explicit*/ DirectoryChangesIterator::DirectoryChangesIterator(
    const void* buffer, const std::size_t max_size)
	: current_(buffer)
	, value_()
	, consumed_size_(0)
	, max_size_(max_size)
{
	if (max_size_ == 0)
	{
		current_ = nullptr;
	}

	if (current_)
	{
		assert(GetInfoSize(current_) <= max_size);
	}
}

/*explicit*/ DirectoryChangesIterator::DirectoryChangesIterator(
	const void* buffer, PortData port_changes)
	: DirectoryChangesIterator(buffer, static_cast<std::size_t>(port_changes.value))
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
	move_to_next();
	const std::size_t size = available_size();
	return DirectoryChangesIterator(current_, size);
}

DirectoryChangesIterator DirectoryChangesIterator::operator++(int)
{
	const std::size_t size = available_size();
	const void* prev = current_;
	move_to_next();
	return DirectoryChangesIterator(prev, size);
}

void DirectoryChangesIterator::move_to_next()
{
	assert(current_);
	assert(consumed_size_ <= max_size_);

	const auto& info = GetInfo(current_);
	consumed_size_ += GetInfoSize(info);
	const bool has_more = (info.NextEntryOffset != 0);
	if (!has_more)
	{
		current_ = nullptr;
		return;
	}

	current_ = (static_cast<const std::uint8_t*>(current_) + info.NextEntryOffset);
	assert((consumed_size_ + GetInfoSize(current_)) <= max_size_);
}

std::size_t DirectoryChangesIterator::available_size() const
{
	assert(max_size_ >= consumed_size_);
	return (max_size_ - consumed_size_);
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
