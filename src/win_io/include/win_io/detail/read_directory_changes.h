#pragma once
#include "io_completion_port.h"

#include <variant>
#include <optional>
#include <string_view>
#include <chrono>
#include <iterator>
#include <utility>
#include <system_error>

#include <cassert>
#include <cstddef>

namespace wi
{
    struct DirectoryChange
    {
        WinDWORD action;
        // Warning: not-null terminated file or folder name that caused `action`.
        std::wstring_view name;

        DirectoryChange(WinDWORD change_action = 0
            , std::wstring_view change_name = {});
    };

    class DirectoryChangesIterator;

    // Parser of `FILE_NOTIFY_INFORMATION` from void* buffer
    // that provides read-only, iterator-like interface.
    class DirectoryChangesRange
    {
    public:
        using iterator = DirectoryChangesIterator;
        using const_iterator = DirectoryChangesIterator;

        // Requires `buffer` to be filled with valid state
        // after one succesfull call to `::ReadDirectoryChangesW()`.
        // (`buffer` is DWORD-aligned, variable length array of
        // `FILE_NOTIFY_INFORMATION` struct).
        explicit DirectoryChangesRange(const void* buffer, std::size_t size);
        explicit DirectoryChangesRange(const void* buffer, PortEntry port_changes);
        explicit DirectoryChangesRange();

        bool has_changes() const;

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

    // Models read-only `ForwardIterator`.
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
        explicit DirectoryChangesIterator(const void* buffer, PortEntry port_changes);

        DirectoryChangesIterator operator++();
        DirectoryChangesIterator operator++(int);

        // Warning: do not save pointer from temporary iterator.
        // No real reference or pointer to persistent memory returned.
        // All variables are created on the fly
        // (and stored in the iterator itself).
        // Something like "arrow proxy" can be used instead:
        // https://quuxplusone.github.io/blog/2019/02/06/arrow-proxy/
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
        // (that requires to return pointer).
        DirectoryChange value_;

        std::size_t consumed_size_;
        std::size_t max_size_;
    };

    bool operator==(const DirectoryChangesIterator& lhs
        , const DirectoryChangesIterator& rhs);
    bool operator!=(const DirectoryChangesIterator& lhs
        , const DirectoryChangesIterator& rhs);
} // namespace wi

namespace wi
{
    inline DirectoryChange::DirectoryChange(WinDWORD change_action /*= 0*/
        , std::wstring_view change_name /*= {}*/)
        : action(change_action)
        , name(std::move(change_name))
    {
    }

    /*explicit*/ inline DirectoryChangesRange::DirectoryChangesRange(
        const void* buffer, std::size_t size)
        : buffer_(buffer)
        , size_(size)
    {
    }

    /*explicit*/ inline DirectoryChangesRange::DirectoryChangesRange(
        const void* buffer, PortEntry port_changes)
        : DirectoryChangesRange(buffer, static_cast<std::size_t>(port_changes.bytes_transferred))
    {
    }

    /*explicit*/ inline DirectoryChangesRange::DirectoryChangesRange()
        : DirectoryChangesRange(nullptr, 0)
    {
    }

    inline bool DirectoryChangesRange::has_changes() const
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
} // namespace wi

namespace wi
{
    // `DirectoryChanges` helper is thin adapter around `IoCompletionPort`
    // that does not own the port. As a result, waiting for directory change
    // can lead to getting some other-not-directory-relative 
    // IoCompletionPort's data since clients of the port can use it in
    // multiple ways (posting custom data, waiting for other changes and so on).
    // Hence, it's a client responsibility to handle wait results.
    // 
    //    DirectoryChangesResults results = dir_changes.get();
    //    if (results.directory_changes()) { /*process directory changes*/ }
    //    else if (results.port_changes()) { /*some other PortData */ }
    // 
    class DirectoryChangesResults
    {
    public:
        explicit DirectoryChangesResults();
        explicit DirectoryChangesResults(DirectoryChangesRange dir_changes);
        explicit DirectoryChangesResults(PortEntry port_changes);

        const DirectoryChangesRange* directory_changes() const;
        const PortEntry* port_changes() const;

    private:
        std::variant<std::monostate, DirectoryChangesRange, PortEntry> data_;
    };

    // Low-level wrapper around `::ReadDirectoryChangesW()`
    // with IOCompletionPort usage.
    // Wait on the data can be done from multiple threads.
    // General use case can look like:
    // 
    //     dir_changes_.start_watch();
    //     while (is_waiting()) {
    //         DirectoryChangesResults results = dir_changes.get();
    //         process_changes(results);
    //         dir_changes_.start_watch();
    //     }
    // 
    class DirectoryChanges
    {
    public:
        // #XXX: use span instead of raw pinter & size.
        static std::optional<DirectoryChanges> make(
            WinHANDLE directory, void* buffer, WinDWORD length
            , bool watch_sub_tree, WinDWORD notify_filter
            , IoCompletionPort& io_port
            , WinULONG_PTR dir_key
            , std::error_code& ec) noexcept;

        static std::optional<DirectoryChanges> make(
            const wchar_t* directory_name
            , void* buffer, WinDWORD length
            , bool watch_sub_tree, WinDWORD notify_filter
            , IoCompletionPort& io_port
            , WinULONG_PTR dir_key
            , std::error_code& ec) noexcept;

        // Construct invalid object. Same as moved-from state.
        explicit DirectoryChanges() noexcept = default;
        DirectoryChanges(const DirectoryChanges&) = delete;
        DirectoryChanges& operator=(const DirectoryChanges&) = delete;
        DirectoryChanges(DirectoryChanges&& rhs) noexcept;
        DirectoryChanges& operator=(DirectoryChanges&& rhs) noexcept;
        ~DirectoryChanges();

        // Call after each successful wait for event
        // (or after `DirectoryChanges` instance creation).
        // Be sure to call only after processing data stored
        // in the `buffer` since system can write into it
        // while you are reading from.
        void start_watch(std::error_code& ec);

        // Blocking call. Wait for changes.
        DirectoryChangesResults get(std::error_code& ec);

        // Non-blocking call.
        DirectoryChangesResults query(std::error_code& ec);

        template<typename Rep, typename Period>
        DirectoryChangesResults wait_for(std::chrono::duration<Rep, Period> time
            , std::error_code& ec);

        WinHANDLE directory_handle() const;

        // Useful when single I/O completion port is used for
        // tracking multiple directories changes.
        // You will need to wait for I/O event and then check
        // from which directory it coming.
        // Once you will need to process data,
        // it's possible to construct `DirectoryChangesRange` from `buffer()`.
        bool is_directory_change(const PortEntry& data) const;

        // Checks whether `data` relates to this directory changes
        // and has no actual changes because of system's buffer overflow.
        bool has_buffer_overflow(const PortEntry& data) const;

        // Returns true when `data` has non-empty set of changes
        // for this instance (i.e, DirectoryChangesRange contains
        // at least one item).
        bool is_valid_directory_change(const PortEntry& data) const;

        const void* buffer() const;

    private:
        static WinHANDLE open_directory(const wchar_t* directory_name, std::error_code& ec);
        void close() noexcept;
        DirectoryChangesResults wait_impl(WinDWORD milliseconds
            , std::error_code& ec);

    private:
        IoCompletionPort* io_port_ = nullptr; // Never null for expected invariants.
        WinHANDLE directory_{};
        void* buffer_ = nullptr;
        WinULONG_PTR dir_key_{};
        WinOVERLAPPED ov_{};
        WinDWORD length_ = 0;
        WinDWORD notify_filter_ = 0;
        bool owns_directory_ = false;
        bool watch_sub_tree_ = false;
    };
} // namespace wi

namespace wi
{
    template<typename Rep, typename Period>
    DirectoryChangesResults DirectoryChanges::wait_for(
        std::chrono::duration<Rep, Period> time, std::error_code& ec)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time);
        return wait_impl(static_cast<WinDWORD>(ms.count()), ec);
    }

    inline DirectoryChangesResults::DirectoryChangesResults()
        : data_()
    {
    }

    inline DirectoryChangesResults::DirectoryChangesResults(DirectoryChangesRange dir_changes)
        : data_(std::move(dir_changes))
    {
    }

    inline DirectoryChangesResults::DirectoryChangesResults(PortEntry port_changes)
        : data_(std::move(port_changes))
    {
    }

    inline const DirectoryChangesRange* DirectoryChangesResults::directory_changes() const
    {
        return std::get_if<DirectoryChangesRange>(&data_);
    }

    inline const PortEntry* DirectoryChangesResults::port_changes() const
    {
        return std::get_if<PortEntry>(&data_);
    }
} // namespace wi

///////////////////////////////////////////////////////////////////////////////
// Implementation that was previously in .cpp file.
// 
namespace wi::detail
{
    static inline const FILE_NOTIFY_INFORMATION& GetInfo(const void* buffer)
    {
        assert(buffer);
        return *static_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
    }

    static inline DirectoryChange GetValue(const void* buffer)
    {
        const auto& info = GetInfo(buffer);
        const std::size_t name_length = (static_cast<std::size_t>(info.FileNameLength) / sizeof(wchar_t));
        DirectoryChange change;
        change.action = info.Action;
        change.name = std::wstring_view(info.FileName, name_length);
        return change;
    }

    static inline std::size_t GetInfoSize(const FILE_NOTIFY_INFORMATION& fi)
    {
        std::size_t size = 0;
        size += sizeof(FILE_NOTIFY_INFORMATION) - sizeof(DWORD);
        size += fi.FileNameLength;
        return size;
    }

    static inline std::size_t GetInfoSize(const void* buffer)
    {
        return GetInfoSize(GetInfo(buffer));
    }
} // namespace wi::detail

namespace wi
{
    /*explicit*/ inline DirectoryChangesIterator::DirectoryChangesIterator()
        : current_(nullptr)
        , value_()
        , consumed_size_(0)
        , max_size_(0)
    {
    }

    /*explicit*/ inline DirectoryChangesIterator::DirectoryChangesIterator(
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
            assert(detail::GetInfoSize(current_) <= max_size);
        }
    }

    /*explicit*/ inline DirectoryChangesIterator::DirectoryChangesIterator(
        const void* buffer, PortEntry port_changes)
        : DirectoryChangesIterator(buffer, static_cast<std::size_t>(port_changes.bytes_transferred))
    {
    }

    inline const DirectoryChange DirectoryChangesIterator::operator*()
    {
        value_ = detail::GetValue(current_);
        return value_;
    }

    inline const DirectoryChange* DirectoryChangesIterator::operator->()
    {
        value_ = detail::GetValue(current_);
        return &value_;
    }

    inline DirectoryChangesIterator DirectoryChangesIterator::operator++()
    {
        move_to_next();
        const std::size_t size = available_size();
        return DirectoryChangesIterator(current_, size);
    }

    inline DirectoryChangesIterator DirectoryChangesIterator::operator++(int)
    {
        const std::size_t size = available_size();
        const void* prev = current_;
        move_to_next();
        return DirectoryChangesIterator(prev, size);
    }

    inline void DirectoryChangesIterator::move_to_next()
    {
        assert(current_);
        assert(consumed_size_ <= max_size_);

        const auto& info = detail::GetInfo(current_);
        consumed_size_ += detail::GetInfoSize(info);
        const bool has_more = (info.NextEntryOffset != 0);
        if (!has_more)
        {
            current_ = nullptr;
            return;
        }

        current_ = (static_cast<const std::uint8_t*>(current_) + info.NextEntryOffset);
        assert((consumed_size_ + detail::GetInfoSize(current_)) <= max_size_);
    }

    inline std::size_t DirectoryChangesIterator::available_size() const
    {
        assert(max_size_ >= consumed_size_);
        return (max_size_ - consumed_size_);
    }

    inline bool operator==(const DirectoryChangesIterator& lhs
        , const DirectoryChangesIterator& rhs)
    {
        return (lhs.current_ == rhs.current_);
    }

    inline bool operator!=(const DirectoryChangesIterator& lhs
        , const DirectoryChangesIterator& rhs)
    {
        return !(lhs == rhs);
    }
} // namespace wi

namespace wi::detail
{
    static inline OVERLAPPED& GetOverlapped(WinOVERLAPPED& ov)
    {
        return *reinterpret_cast<OVERLAPPED*>(&ov);
    }
} // namespace

namespace wi
{
    inline /*static*/ std::optional<DirectoryChanges> DirectoryChanges::make(
        WinHANDLE directory, void* buffer
        , WinDWORD length, bool watch_sub_tree, WinDWORD notify_filter
        , IoCompletionPort& io_port, WinULONG_PTR dir_key
        , std::error_code& ec) noexcept
    {
        assert((reinterpret_cast<std::uintptr_t>(buffer) % sizeof(WinDWORD) == 0)
            && "[Dc] Buffer should be DWORD aligned");
        assert(((directory != INVALID_HANDLE_VALUE) && (directory != nullptr))
            && "[Dc] Directory should be valid");

        io_port.associate_device(directory, dir_key, ec);
        if (ec)
        {
            return std::nullopt;
        }

        std::optional<DirectoryChanges> value;
        DirectoryChanges& o = value.emplace();
        o.io_port_ = &io_port;
        o.directory_ = directory;
        o.buffer_ = buffer;
        o.dir_key_ = dir_key;
        o.ov_ = {};
        o.length_ = length;
        o.notify_filter_ = notify_filter;
        o.owns_directory_ = false;
        o.watch_sub_tree_ = watch_sub_tree;
        return value;
    }

    inline /*static*/ std::optional<DirectoryChanges> DirectoryChanges::make(
        const wchar_t* directory_name
        , void* buffer, WinDWORD length
        , bool watch_sub_tree, WinDWORD notify_filter
        , IoCompletionPort& io_port, WinULONG_PTR dir_key
        , std::error_code& ec) noexcept
    {
        WinHANDLE directory = open_directory(directory_name, ec);
        if (ec)
        {
            return std::nullopt;
        }

        if (auto o = make(directory, buffer, length, watch_sub_tree, notify_filter, io_port, dir_key, ec);
            o.has_value() && !ec)
        {
            o->owns_directory_ = true;
            return o;
        }

        (void)::CloseHandle(directory);
        return std::nullopt;
    }

    inline DirectoryChanges::DirectoryChanges(DirectoryChanges&& rhs) noexcept
        : io_port_(std::exchange(rhs.io_port_, nullptr))
        , directory_(std::exchange(rhs.directory_, INVALID_HANDLE_VALUE))
        , buffer_(std::exchange(rhs.buffer_, nullptr))
        , dir_key_(std::exchange(rhs.dir_key_, 0))
        , ov_{}
        , length_(std::exchange(rhs.length_, 0))
        , notify_filter_(std::exchange(rhs.notify_filter_, 0))
        , owns_directory_(std::exchange(rhs.owns_directory_, false))
        , watch_sub_tree_(std::exchange(rhs.watch_sub_tree_, false))
    {
        ov_ = rhs.ov_;
    }

    inline DirectoryChanges& DirectoryChanges::operator=(DirectoryChanges&& rhs) noexcept
    {
        if (this == &rhs)
        {
            return *this;
        }
        close();

        io_port_ = std::exchange(rhs.io_port_, nullptr);
        directory_ = std::exchange(rhs.directory_, INVALID_HANDLE_VALUE);
        buffer_ = std::exchange(rhs.buffer_, nullptr);
        dir_key_ = std::exchange(rhs.dir_key_, 0);
        ov_ = rhs.ov_;
        length_ = std::exchange(rhs.length_, 0);
        notify_filter_ = std::exchange(rhs.notify_filter_, 0);
        owns_directory_ = std::exchange(rhs.owns_directory_, false);
        watch_sub_tree_ = std::exchange(rhs.watch_sub_tree_, false);
        return *this;
    }

    inline /*static*/ WinHANDLE DirectoryChanges::open_directory(const wchar_t* directory_name, std::error_code& ec)
    {
        const auto handle = ::CreateFileW(directory_name
            , FILE_LIST_DIRECTORY
            // Anyone can do whatever needed.
            , FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE
            , nullptr
            , OPEN_EXISTING
            // Open as directory, async.
            , FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
            , nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            ec = detail::make_last_error_code();
        }
        return handle;
    }

    inline WinHANDLE DirectoryChanges::directory_handle() const
    {
        return directory_;
    }

    inline const void* DirectoryChanges::buffer() const
    {
        return buffer_;
    }

    inline DirectoryChanges::~DirectoryChanges()
    {
        close();
    }

    inline void DirectoryChanges::close() noexcept
    {
        if (owns_directory_)
        {
            (void)::CloseHandle(directory_);
            directory_ = nullptr;
            owns_directory_ = false;
        }
    }

    inline void DirectoryChanges::start_watch(std::error_code& ec)
    {
        ec = std::error_code();
        // See ::ReadDirectoryChangesExW(): starting from Windows 10.
        const BOOL status = ::ReadDirectoryChangesW(directory_
            , buffer_, length_, watch_sub_tree_, notify_filter_
            , nullptr, &detail::GetOverlapped(ov_), nullptr);
        if (!status)
        {
            ec = detail::make_last_error_code();
        }
    }

    inline bool DirectoryChanges::is_directory_change(const PortEntry& data) const
    {
        return (data.completion_key == dir_key_) && (data.overlapped == &ov_);
    }

    inline bool DirectoryChanges::has_buffer_overflow(const PortEntry& data) const
    {
        const auto buffer_size = data.bytes_transferred;
        return is_directory_change(data)
            && (buffer_size == 0);
    }

    inline bool DirectoryChanges::is_valid_directory_change(const PortEntry& data) const
    {
        const auto buffer_size = data.bytes_transferred;
        return is_directory_change(data)
            && (buffer_size != 0);
    }

    inline DirectoryChangesResults DirectoryChanges::wait_impl(
        WinDWORD milliseconds, std::error_code& ec)
    {
        auto data = io_port_->wait_for(std::chrono::milliseconds(milliseconds), ec);
        if (!data)
        {
            return DirectoryChangesResults();
        }
        // Check if data is coming from our directory, since we do not
        // own I/O Completion Port and it can be used for other purpose.
        // Also we check whether there is actual data (i.e, no buffer overflow).
        if (!is_valid_directory_change(*data))
        {
            return DirectoryChangesResults(std::move(*data));
        }
        return DirectoryChangesResults(DirectoryChangesRange(buffer_, std::move(*data)));
    }

    inline DirectoryChangesResults DirectoryChanges::get(std::error_code& ec)
    {
        return wait_impl(INFINITE, ec);
    }

    inline DirectoryChangesResults DirectoryChanges::query(std::error_code& ec)
    {
        return wait_impl(0, ec);
    }
} // namespace wi
