#pragma once
#include <win_io/detail/io_completion_port.h>
#include <win_io/detail/directory_changes_range.h>

#include <variant>

namespace wi
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

        const DirectoryChangesRange* directory_changes() const;
        const PortData* port_changes() const;

    private:
        std::variant<std::monostate, DirectoryChangesRange, PortData> data_;
    };

    // Low-level wrapper around `::ReadDirectoryChangesW()`
    // with IOCompletionPort usage.
    // Wait on the data can be done from multiple threads.
    // General use case can look like:
    // 
    // dir_changes_.start_watch();
    // while (is_waiting()) {
    //        DirectoryChangesResults results = dir_changes.get();
    //        process_changes(results);
    //        dir_changes_.start_watch();
    // }
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
        bool is_directory_change(const PortData& data) const;

        // Checks whether `data` relates to this directory changes
        // and has no actual changes because of system's buffer overflow.
        bool has_buffer_overflow(const PortData& data) const;

        // Returns true when `data` has non-empty set of changes
        // for this instance (i.e, DirectoryChangesRange contains
        // at least one item).
        bool is_valid_directory_change(const PortData& data) const;

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

    inline DirectoryChangesResults::DirectoryChangesResults(PortData port_changes)
        : data_(std::move(port_changes))
    {
    }

    inline const DirectoryChangesRange* DirectoryChangesResults::directory_changes() const
    {
        return std::get_if<DirectoryChangesRange>(&data_);
    }

    inline const PortData* DirectoryChangesResults::port_changes() const
    {
        return std::get_if<PortData>(&data_);
    }
} // namespace wi
