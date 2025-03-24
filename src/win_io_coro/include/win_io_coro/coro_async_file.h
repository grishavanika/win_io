#pragma once
#include <win_io/io_completion_port.h>

#include <system_error>
#include <coroutine>

#include <cassert>

#include <Windows.h>

namespace wi::coro
{
    class AsyncFile;

    class ReadBuffer
    {
    public:
        explicit ReadBuffer(std::uint8_t* pages_start, std::uint64_t user_offset, std::uint64_t user_size);
        std::span<std::uint8_t> GetData() const noexcept;

    public:
        ReadBuffer() = default;
        ReadBuffer(const ReadBuffer&) = delete;
        ReadBuffer& operator=(const ReadBuffer&) = delete;
        ReadBuffer& operator=(ReadBuffer&& rhs) noexcept;
        ReadBuffer(ReadBuffer&& rhs) noexcept;
        ~ReadBuffer();

    private:
        std::uint8_t* _pages_start = nullptr;
        std::uint64_t _user_offset = 0;
        std::uint64_t _user_size = 0;
    };

    struct ReadResult
    {
        std::error_code error;
        ReadBuffer buffer;
    };

    // Can't be destroyed while read is in progress.
    // It's possible to cancel & block in destructor if needed.
    class AsyncReadTask
    {
    public:
        explicit AsyncReadTask(AsyncFile& file, std::uint64_t offset, std::uint32_t size);

        AsyncReadTask(AsyncReadTask&& rhs) = delete;
        AsyncReadTask& operator=(AsyncReadTask&& rhs) = delete;
        AsyncReadTask(const AsyncReadTask& rhs) = delete;
        AsyncReadTask& operator=(const AsyncReadTask& rhs) = delete;

        bool await_ready() const noexcept;
        bool await_suspend(std::coroutine_handle<> awaiter) noexcept;
        ReadResult await_resume() noexcept;

        // Called by the IOCP scheduler.
        void on_end(std::error_code ec, ReadBuffer data);

    private:
        AsyncFile* _file;
        std::uint64_t _offset;
        std::uint32_t _size;
        std::error_code _error;
        ReadBuffer _data;
        std::coroutine_handle<> _awaiter;
    };

    class AsyncFile
    {
    public:
        static AsyncFile open(IoCompletionPort& iocp
            , const char* file_path
            , std::error_code& ec);

        HANDLE native_handle() const;
        std::uint64_t file_size() const;
        AsyncReadTask read(std::uint64_t offset, std::uint32_t size);

        AsyncFile(const AsyncFile&) = delete;
        AsyncFile& operator=(const AsyncFile&) = delete;
        AsyncFile(AsyncFile&& rhs) noexcept;
        AsyncFile& operator=(AsyncFile&& rhs) noexcept;
        ~AsyncFile();

    public:
        static constexpr ULONG_PTR kAsyncIOCPFileKey = 42;

    private:
        explicit AsyncFile(HANDLE handle, IoCompletionPort& iocp, std::uint64_t file_size);
        void close_file();

    private:
        HANDLE _file_handle;
        IoCompletionPort* _iocp;
        std::uint64_t _file_size;
    };

    std::size_t HandleIOCP_Once(IoCompletionPort& iocp);
} // namespace wi::coro

namespace wi::coro
{
    namespace detail
    {
        inline std::error_code make_last_error_code(
            DWORD last_error = ::GetLastError())
        {
            // Using `system_category` with implicit assumption that
            // MSVC's implementation will add proper error code message for free
            // if using together with `std::system_error`.
            return std::error_code(static_cast<int>(last_error), std::system_category());
        }
    } // namespace wi

    /*static*/ AsyncFile AsyncFile::open(IoCompletionPort& iocp
        , const char* file_path
        , std::error_code& ec)
    {
        const HANDLE handle = ::CreateFileA(file_path
            , GENERIC_READ
            , FILE_SHARE_READ
            , nullptr
            , OPEN_EXISTING
            , FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING
            , nullptr);
        AsyncFile file(handle, iocp, 0); // close on early return.
        if (handle == INVALID_HANDLE_VALUE)
        {
            ec = detail::make_last_error_code();
            return file;
        }
        // https://devblogs.microsoft.com/oldnewthing/20200221-00/?p=103466
        // https://www.microsoftpressstore.com/articles/article.aspx?p=2224047&seqNum=5
        BOOL ok = ::SetFileCompletionNotificationModes(handle
            , FILE_SKIP_SET_EVENT_ON_HANDLE
            | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
        if (not ok)
        {
            ec = detail::make_last_error_code();
            return file;
        }

        LARGE_INTEGER li{};
        ok = ::GetFileSizeEx(handle, &li);
        if (not ok)
        {
            ec = detail::make_last_error_code();
            return file;
        }
        file._file_size = std::uint64_t(li.QuadPart);

        iocp.associate_device(handle, kAsyncIOCPFileKey, ec);

        return file;
    }

    /*explicit*/ inline AsyncFile::AsyncFile(HANDLE handle, IoCompletionPort& iocp, std::uint64_t file_size)
        : _file_handle(handle)
        , _iocp(&iocp)
        , _file_size(file_size)
    {
    }

    inline void AsyncFile::close_file()
    {
        HANDLE to_close = std::exchange(_file_handle, INVALID_HANDLE_VALUE);
        _file_size = 0;
        _iocp = nullptr;
        if (to_close != INVALID_HANDLE_VALUE)
        {
            (void)::CloseHandle(to_close);
        }
    }

    inline AsyncFile::~AsyncFile()
    {
        close_file();
    }

    inline AsyncFile::AsyncFile(AsyncFile&& rhs) noexcept
        : _file_handle(std::exchange(rhs._file_handle, INVALID_HANDLE_VALUE))
        , _iocp(std::exchange(rhs._iocp, nullptr))
        , _file_size(std::exchange(rhs._file_size, 0))
    {
    }

    inline AsyncFile& AsyncFile::operator=(AsyncFile&& rhs) noexcept
    {
        if (this != &rhs)
        {
            close_file();
            _file_handle = std::exchange(rhs._file_handle, INVALID_HANDLE_VALUE);
            _iocp = std::exchange(rhs._iocp, nullptr);
            _file_size = std::exchange(rhs._file_size, 0);
        }
        return *this;
    }

    inline HANDLE AsyncFile::native_handle() const
    {
        return _file_handle;
    }

    inline std::uint64_t AsyncFile::file_size() const
    {
        return _file_size;
    }

    inline AsyncReadTask AsyncFile::read(std::uint64_t offset, std::uint32_t size)
    {
        return AsyncReadTask(*this, offset, size);
    }

    /*explicit*/ inline AsyncReadTask::AsyncReadTask(AsyncFile& file
        , std::uint64_t offset, std::uint32_t size)
            : _file(&file)
            , _offset(offset)
            , _size(size)
    {
    }

    inline bool AsyncReadTask::await_ready() const noexcept
    {
        // Let's schedule read on suspend.
        return false;
    }

    inline ReadResult AsyncReadTask::await_resume() noexcept
    {
        return ReadResult{_error, std::move(_data)};
    }

    inline void AsyncReadTask::on_end(std::error_code ec, ReadBuffer data)
    {
        assert(_awaiter);
        _error = ec;
        _data = std::move(data);
        _awaiter.resume();
    }

    /*explicit*/ inline ReadBuffer::ReadBuffer(std::uint8_t* pages_start
        , std::uint64_t user_offset
        , std::uint64_t user_size)
            : _pages_start(pages_start)
            , _user_offset(user_offset)
            , _user_size(user_size)
    {
    }

    inline std::span<std::uint8_t> ReadBuffer::GetData() const noexcept
    {
        assert(_pages_start);
        return std::span<std::uint8_t>(_pages_start + _user_offset, std::size_t(_user_size));
    }

    inline ReadBuffer& ReadBuffer::operator=(ReadBuffer&& rhs) noexcept
    {
        if (this != &rhs)
        {
            this->~ReadBuffer();
            _pages_start = std::exchange(rhs._pages_start, nullptr);
            _user_offset = std::exchange(rhs._user_offset, 0);
            _user_size = std::exchange(rhs._user_size, 0);
        }
        return *this;
    }

    inline ReadBuffer::ReadBuffer(ReadBuffer&& rhs) noexcept
        : _pages_start(std::exchange(rhs._pages_start, nullptr))
        , _user_offset(std::exchange(rhs._user_offset, 0))
        , _user_size(std::exchange(rhs._user_size, 0))
    {
    }

    inline ReadBuffer::~ReadBuffer()
    {
        if (_pages_start)
        {
            (void)::VirtualFree(_pages_start, 0, MEM_RELEASE);
            _pages_start = nullptr;
        }
    }

    // #XXX: temporary. Use API to know for real.
    constexpr std::uint64_t kSectorSize = 4 * 1024;

    static std::uint64_t Rounddowmn(std::uint64_t value, std::uint64_t multiple)
    {
        assert(multiple > 0);
        return ((value / multiple) * multiple);
    }

    static std::uint64_t Roundup(std::uint64_t value, std::uint64_t multiple)
    {
        assert(multiple > 0);
        return Rounddowmn(value, multiple) +
            (((value % multiple) > 0) ? multiple : 0);
    }

    // https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering.
    // #XXX: physical sector size VS logical sector size.
    struct SizeOffsetBySector
    {
        std::uint64_t _offset = 0;
        std::uint64_t _size = 0;

        static SizeOffsetBySector FromAnyOffsetAndSize(std::uint64_t any_offset, std::uint64_t any_size)
        {
            SizeOffsetBySector by_sectors;
            by_sectors._offset = Rounddowmn(any_offset, kSectorSize);
            by_sectors._size = (Roundup(any_offset + any_size, kSectorSize) - by_sectors._offset);
            return by_sectors;
        }
    };

    template<std::size_t Alignment>
    static std::uint8_t* GetAligned(std::uint8_t* base)
    {
        static_assert(std::has_single_bit(Alignment)); // power of 2.
        assert(base);

        constexpr std::uintptr_t mask = ~static_cast<std::uintptr_t>(Alignment - 1);
        const std::uintptr_t memory = (reinterpret_cast<std::uintptr_t>(base) + Alignment - 1) & mask;
        std::uint8_t* const ptr = reinterpret_cast<std::uint8_t*>(memory);
        assert((std::uint64_t(ptr) % Alignment) == 0);
        return ptr;
    }

    struct SingleReadOverlapped : OVERLAPPED
    {
        std::uint64_t _user_offset; // #
        std::uint64_t _user_size;   // # Not needed. Stored in `AsyncReadTask* _callback`.
        std::uint8_t* _pages_start;
        std::uint64_t _overlapped_size;
        AsyncReadTask* _callback;

        SingleReadOverlapped()
            : OVERLAPPED()
            , _user_offset(0)
            , _user_size(0)
            , _pages_start(nullptr)
            , _overlapped_size(0)
            , _callback(nullptr)
        {
        }

        ~SingleReadOverlapped() = default;

        std::uint8_t* GetReadBufferStart() const
        {
            return (_pages_start + _overlapped_size);
        }

        static SingleReadOverlapped* EmplaceIntoPage(std::uint8_t* pages_start, std::uint64_t overlapped_size)
        {
            assert(pages_start);
            std::uint8_t* ptr = GetAligned<alignof(SingleReadOverlapped)>(pages_start);
            assert((ptr + sizeof(SingleReadOverlapped)) <= (ptr + overlapped_size));
            auto* ovs = new(static_cast<void*>(ptr)) SingleReadOverlapped();
            ovs->_pages_start = pages_start;
            ovs->_overlapped_size = overlapped_size;
            return ovs;
        }

        void InvokeReadEnd(DWORD read_bytes)
        {
            std::printf("ok\n");

            if (Internal != 0)
            {
                InvokeReadFail(DWORD(Internal));
                return;
            }
            assert(read_bytes == InternalHigh);

            const auto io_sizes = SizeOffsetBySector::FromAnyOffsetAndSize(_user_offset, _user_size);
            assert(_user_offset >= io_sizes._offset);
            // We read `delta` bytes more then needed.
            const std::uint64_t delta = (_user_offset - io_sizes._offset);
            if (read_bytes < delta)
            {
                InvokeReadFail(ERROR_INVALID_DATA);
                return;
            }
            const std::uint64_t size = (std::min)(std::uint64_t(read_bytes - delta), _user_size);
            // #XXX: can be end of file. Consider reading less then requested as success.
            if (size != _user_size)
            {
                InvokeReadFail(ERROR_BAD_LENGTH);
                return;
            }

            ReadBuffer buffer(_pages_start
                // There is `_overlapped_size` region reserved for the request;
                // only then there is read buffer start. Which contains
                // `delta` bytes of unneeded read.
                , (_overlapped_size + delta)
                , _user_size);
            AsyncReadTask* callback = _callback;
            this->~SingleReadOverlapped();
            // DONT touch any member now.

            callback->on_end(std::error_code(), std::move(buffer));
        }

        void InvokeReadFail(DWORD last_error)
        {
            std::printf("error\n");

            AsyncReadTask* callback = _callback;
            (void)::VirtualFree(_pages_start, 0, MEM_RELEASE);
            this->~SingleReadOverlapped();
            // DONT touch any member now.

            callback->on_end(wi::detail::make_last_error_code(last_error), ReadBuffer{});
        }

        SingleReadOverlapped(const SingleReadOverlapped&) = delete;
        SingleReadOverlapped& operator=(const SingleReadOverlapped&) = delete;
        SingleReadOverlapped(SingleReadOverlapped&&) = delete;
        SingleReadOverlapped& operator=(SingleReadOverlapped&&) = delete;
    };

    inline std::uint64_t MaxReadSizePerSingleCall()
    {
        constexpr DWORD max_read = (std::numeric_limits<DWORD>::max)();
        const std::uint64_t by_sector = Rounddowmn(max_read, kSectorSize);
        assert(by_sector <= max_read);
        return by_sector;
    }

    static std::error_code ScheduleReadImpl_(HANDLE file
        , std::uint64_t user_offset
        , std::uint64_t user_size
        , AsyncReadTask& on_finish)
    {
        assert(file != INVALID_HANDLE_VALUE);
        assert(user_size != 0);
        assert(user_offset <= MaxReadSizePerSingleCall());

        // Reserve page(s) for OVERLAPPED/request management bookkeeping.
        std::uint64_t overlapped_size = sizeof(SingleReadOverlapped);
        static_assert((kSectorSize % alignof(SingleReadOverlapped)) == 0);
#if (0)
        if ((kSectorSize % alignof(SingleReadOverlapped)) != 0)
        {
            // Additional memory to compensate alignment.
            overlapped_size += (alignof(SingleReadOverlapped) - 1);
        }
#endif
        // Waste whole page anyway.
        overlapped_size = Roundup(overlapped_size, kSectorSize);

        const auto io_size = SizeOffsetBySector::FromAnyOffsetAndSize(user_offset, user_size);
        const SIZE_T all_memory = SIZE_T(io_size._size + overlapped_size);
        std::uint8_t* pages_start = static_cast<std::uint8_t*>(
            ::VirtualAlloc(nullptr
                , all_memory
                , MEM_COMMIT
                , PAGE_READWRITE));
        assert(pages_start);
        // #XXX: system's page size granularity. May not match to logical
        // and/or physical sector size needed.
        assert((std::uint64_t(pages_start) % kSectorSize) == 0);

        const ULARGE_INTEGER offset{ .QuadPart = io_size._offset };
        auto* ov = SingleReadOverlapped::EmplaceIntoPage(pages_start, overlapped_size);
        ov->Offset = offset.LowPart;
        ov->OffsetHigh = offset.HighPart;
        ov->hEvent = nullptr;
        ov->_callback = &on_finish;
        ov->_user_offset = user_offset;
        ov->_user_size = user_size;

        std::printf("started\n");
        const BOOL read_finished = ::ReadFile(file
            , ov->GetReadBufferStart()
            , DWORD(io_size._size)
            , nullptr
            , ov);
        const DWORD last_error = ::GetLastError();
        if (read_finished)
        {
            ov->InvokeReadEnd(DWORD(io_size._size));
        }
        else if (!read_finished && (last_error != ERROR_IO_PENDING))
        {
            ov->InvokeReadFail(last_error);
        }

        std::printf("schedule end\n");
        return std::error_code();
    }

    bool AsyncReadTask::await_suspend(std::coroutine_handle<> awaiter) noexcept
    {
        _awaiter = awaiter;
        _error = ScheduleReadImpl_(_file->native_handle(), _offset, _size, *this);
        return (not _error);
    }

    // Temporary, as an example of how to handle.
    inline std::size_t HandleIOCP_Once(IoCompletionPort& iocp)
    {
        std::error_code ec;

        PortEntry entries[64];
        auto ready = iocp.get_many(entries, ec);
        if (ec)
        {
            for (PortEntry& entry : ready)
            {
                assert(entry.completion_key == AsyncFile::kAsyncIOCPFileKey);
                assert(entry.overlapped);
                auto* ov = static_cast<SingleReadOverlapped*>(entry.overlapped);
                ov->InvokeReadFail(ec.value());
            }
        }
        else
        {
            for (PortEntry& entry : ready)
            {
                assert(entry.completion_key == AsyncFile::kAsyncIOCPFileKey);
                assert(entry.overlapped);
                auto* ov = static_cast<SingleReadOverlapped*>(entry.overlapped);
                ov->InvokeReadEnd(DWORD(entry.bytes_transferred));
            }
        }
        return ready.size();
    }

} // namespace wi::coro
