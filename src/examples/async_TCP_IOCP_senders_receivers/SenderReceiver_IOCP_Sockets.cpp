#include <winsock2.h>
#include <ws2tcpip.h>

#if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN
#endif
#if !defined(_WINSOCKAPI_)
#  define _WINSOCKAPI_
#endif
#include <Mswsock.h>

#pragma comment(lib, "Ws2_32.lib")

#include <win_io/detail/io_completion_port.h>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

#include <unifex/sender_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/sequence.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/defer.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/on.hpp>

#define XX_ENABLE_OPERATION_LOGS() 0
#define XX_ENABLE_SENDERS_LOGS() 0

static constexpr wi::WinULONG_PTR kClientKeyIOCP = 1;

struct IOCP_Overlapped : OVERLAPPED
{
    using Handle = void (*)(void* user_data, const wi::PortEntry& /*entry*/, std::error_code /*ec*/);
    Handle _callback = nullptr;
    void* _user_data = nullptr;
};

// Hack to make set_void() (no T) work with unifex::then(). Bug ?
using void_ = struct {};

struct Operation_Log
{
    Operation_Log(const Operation_Log&) = delete;
    Operation_Log& operator=(const Operation_Log&) = delete;
    Operation_Log& operator=(Operation_Log&&) = delete;
    // Needed for composition. Someone can move-construct
    // Operation state into own/internal percistent storage
    // with a call to unifex::connect().
    Operation_Log(Operation_Log&&) noexcept = default;

protected:
    const char* _name = nullptr;

    Operation_Log(const char* name)
        : _name(name)
    {
#if (XX_ENABLE_OPERATION_LOGS())
        printf("[State] '%s' c-tor.\n", _name);
#endif
    }
    ~Operation_Log()
    {
#if (XX_ENABLE_OPERATION_LOGS())
        printf("[State] '%s' d-tor.\n", _name);
#endif
    }

    void log(const char* debug)
    {
#if (XX_ENABLE_OPERATION_LOGS())
        printf("[State] '%s' - %s.\n", _name, debug);
#else
        (void)debug;
#endif
    }
};

template<typename V, typename E>
struct Sender_LogSimple
{
    template<template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<V>>;
    template <template <typename...> class Variant>
    using error_types = Variant<E>;
    static constexpr bool sends_done = true;

    Sender_LogSimple(const Sender_LogSimple&) = delete;
    Sender_LogSimple& operator=(const Sender_LogSimple&) = delete;
    Sender_LogSimple& operator=(Sender_LogSimple&&) = delete;
    // Needed.
    Sender_LogSimple(Sender_LogSimple&&) noexcept = default;

protected:
    const char* _name = nullptr;

    Sender_LogSimple(const char* name)
        : _name(name)
    {
#if (XX_ENABLE_SENDERS_LOGS())
        printf("[Sender] '%s' c-tor.\n", _name);
#endif
    }
    ~Sender_LogSimple() noexcept
    {
#if (XX_ENABLE_SENDERS_LOGS())
        printf("[Sender] '%s' d-tor.\n", _name);
#endif
    }
    void log(const char* debug)
    {
#if (XX_ENABLE_SENDERS_LOGS())
        printf("[State] '%s' - %s.\n", _name, debug);
#else
        (void)debug;
#endif
    }
};

struct Endpoint_IPv4
{
    std::uint16_t _port_network = 0;
    std::uint32_t _ip_network = 0;

    // https://man7.org/linux/man-pages/man3/inet_pton.3.html
    // `src` is in dotted-decimal format, "ddd.ddd.ddd.ddd".
    static std::optional<Endpoint_IPv4> from_string(const char* src, std::uint16_t port_host)
    {
        struct in_addr ipv4{};
        static_assert(sizeof(ipv4.s_addr) == sizeof(std::uint32_t));
        const int ok = inet_pton(AF_INET, src, &ipv4);
        if (ok == 1)
        {
            Endpoint_IPv4 endpoint;
            endpoint._port_network = ::htons(port_host);
            endpoint._ip_network = ipv4.s_addr;
            return endpoint;
        }
        return std::nullopt;
    }
};

template<typename Receiver>
struct Operation_Connect : Operation_Log
{
    explicit Operation_Connect(Receiver&& receiver, SOCKET socket, Endpoint_IPv4 endpoint)
        : Operation_Log("connect")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_Connect::on_connected, this}
        , _socket(socket)
        , _endpoint(endpoint) {}

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    Endpoint_IPv4 _endpoint;

    static void on_connected(void* user_data, const wi::PortEntry& entry, std::error_code ec)
    {
        assert(user_data);
        Operation_Connect& self = *static_cast<Operation_Connect*>(user_data);
        self.log("on_connected");

        assert(entry.bytes_transferred == 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == &self._ov);

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

        const int error = ::setsockopt(self._socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
        assert(error == 0);

        unifex::set_value(std::move(self._receiver), void_());
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_Connect& self) noexcept
    {
        self.log("start");

        assert(self._socket != INVALID_SOCKET);

        LPFN_CONNECTEX ConnectEx = nullptr;
        {
            GUID GuidConnectEx = WSAID_CONNECTEX;
            DWORD bytes = 0;
            const int error = ::WSAIoctl(self._socket
                , SIO_GET_EXTENSION_FUNCTION_POINTER
                , &GuidConnectEx, sizeof(GuidConnectEx)
                , &ConnectEx, sizeof(ConnectEx)
                , &bytes, nullptr, nullptr);
            assert(error == 0);
            assert(ConnectEx);
        }

        { // Required by ConnectEx().
            struct sockaddr_in local_address{};
            local_address.sin_family = AF_INET;
            local_address.sin_addr.s_addr = INADDR_ANY;
            local_address.sin_port = 0;
            const int error = ::bind(self._socket
                , reinterpret_cast<SOCKADDR*>(&local_address)
                , sizeof(local_address));
            assert(error == 0);
        }

        struct sockaddr_in connect_to{};
        connect_to.sin_family = AF_INET;
        connect_to.sin_port = self._endpoint._port_network;
        connect_to.sin_addr.s_addr = self._endpoint._ip_network;

        const BOOL finished = ConnectEx(self._socket
            , (sockaddr*)&connect_to
            , sizeof(connect_to)
            , nullptr
            , 0
            , nullptr
            , &self._ov);
        if (finished)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = static_cast<LPOVERLAPPED>(&self._ov);
            self.on_connected(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = static_cast<LPOVERLAPPED>(&self._ov);
            self.on_connected(&self, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }

        // In-progress.
    }
};

struct Sender_Connect : Sender_LogSimple<void_, std::error_code>
{
    explicit Sender_Connect(SOCKET socket, Endpoint_IPv4 endpoint) noexcept
        : Sender_LogSimple<void_, std::error_code>("connect")
        , _socket(socket)
        , _endpoint(endpoint) { }

    SOCKET _socket = INVALID_SOCKET;
    Endpoint_IPv4 _endpoint;

    template<typename This, typename Receiver>
        requires std::is_same_v<Sender_Connect, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_Connect<Reveiver_>(std::move(receiver)
            , self._socket, self._endpoint);
    }
};

template<typename Receiver>
struct Operation_WriteSome : Operation_Log
{
    explicit Operation_WriteSome(Receiver&& receiver, SOCKET socket, std::span<char> data)
        : Operation_Log("write_some")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_WriteSome::on_sent, this}
        , _socket(socket)
        , _data(data) { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _data;

    static void on_sent(void* user_data, const wi::PortEntry& entry, std::error_code ec)
    {
        assert(user_data);
        Operation_WriteSome& self = *static_cast<Operation_WriteSome*>(user_data);
        self.log("on_sent");

        assert(entry.bytes_transferred > 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == &self._ov);

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

        unifex::set_value(std::move(self._receiver)
            , std::size_t(entry.bytes_transferred));
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_WriteSome& self) noexcept
    {
        self.log("start");

        assert(self._data.size() > 0);

        WSABUF send_buffer{};
        send_buffer.buf = self._data.data();
        send_buffer.len = ULONG(self._data.size());
        const int error = ::WSASend(self._socket
            , &send_buffer, 1
            , nullptr/*bytes_send*/
            , 0/*flags*/
            , &self._ov, nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = wi::WinDWORD(self._data.size());
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = static_cast<LPOVERLAPPED>(&self._ov);
            self.on_sent(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = static_cast<LPOVERLAPPED>(&self._ov);
            self.on_sent(&self, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

struct Sender_WriteSome : Sender_LogSimple<std::size_t, std::error_code>
{
    explicit Sender_WriteSome(SOCKET socket, std::span<char> data) noexcept
        : Sender_LogSimple<std::size_t, std::error_code>("send_some")
        , _socket(socket)
        , _data(data) { }

    SOCKET _socket;
    std::span<char> _data;

    template <typename This, typename Receiver>
        requires std::is_same_v<Sender_WriteSome, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_WriteSome<Reveiver_>(std::move(receiver), self._socket, self._data);
    }
};

template<typename Receiver>
struct Operation_ReadSome : Operation_Log
{
    explicit Operation_ReadSome(Receiver&& receiver, SOCKET socket, std::span<char> buffer)
        : Operation_Log("receive_some")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_ReadSome::on_received, this}
        , _socket(socket)
        , _buffer(buffer)
        , _flags(0) { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _buffer;
    DWORD _flags;

    static void on_received(void* user_data, const wi::PortEntry& entry, std::error_code ec)
    {
        assert(user_data);
        Operation_ReadSome& self = *static_cast<Operation_ReadSome*>(user_data);
        self.log("on_received");

        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == &self._ov);

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

        assert(entry.bytes_transferred > 0);
        unifex::set_value(std::move(self._receiver)
            , std::size_t(entry.bytes_transferred));
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_ReadSome& self) noexcept
    {
        self.log("start");

        assert(self._buffer.size() > 0);

        WSABUF receive_buffer{};
        receive_buffer.buf = self._buffer.data();
        receive_buffer.len = ULONG(self._buffer.size());
        self._flags = MSG_PARTIAL;
        DWORD received = 0;
        const int error = ::WSARecv(self._socket
            , &receive_buffer, 1
            , &received
            , &self._flags
            , &self._ov
            , nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = received;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = static_cast<LPOVERLAPPED>(&self._ov);
            self.on_received(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = static_cast<LPOVERLAPPED>(&self._ov);
            self.on_received(&self, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};
struct Sender_ReadSome : Sender_LogSimple<std::size_t, std::error_code>
{
    Sender_ReadSome(SOCKET socket, std::span<char> buffer) noexcept
        : Sender_LogSimple<size_t, std::error_code>("read_some")
        , _socket(socket)
        , _buffer(buffer) { }

    SOCKET _socket;
    std::span<char> _buffer;

    template<typename This, typename Receiver>
        requires std::is_same_v<Sender_ReadSome, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_ReadSome<Reveiver_>(std::move(receiver), self._socket, self._buffer);
    }
};

struct Async_TCPSocket
{
    SOCKET _socket = INVALID_SOCKET;
    wi::IoCompletionPort* _iocp = nullptr;

    explicit Async_TCPSocket() = default;

    static std::optional<Async_TCPSocket> make(wi::IoCompletionPort& iocp)
    {
        WSAPROTOCOL_INFOW* default_protocol = nullptr;
        GROUP no_group = 0;
        SOCKET handle = ::WSASocketW(AF_INET
            , SOCK_STREAM
            , IPPROTO_TCP
            , default_protocol
            , no_group
            , WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        Async_TCPSocket socket(handle, iocp);
        if (handle == INVALID_SOCKET)
        {
            return std::nullopt;
        }

        const BOOL ok = ::SetFileCompletionNotificationModes(HANDLE(handle)
            , FILE_SKIP_SET_EVENT_ON_HANDLE
            | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
        if (!ok)
        {
            return std::nullopt;
        }

        u_long flags = 1; // non-blocking, enable.
        const int error = ::ioctlsocket(handle, FIONBIO, &flags);
        if (error)
        {
            return std::nullopt;
        }

        std::error_code ec;
        iocp.associate_socket(wi::WinSOCKET(handle), kClientKeyIOCP, ec);
        if (ec)
        {
            return std::nullopt;
        }
        return std::make_optional(std::move(socket));
    }

    Sender_Connect async_connect(Endpoint_IPv4 endpoint)
    {
        return Sender_Connect{_socket, endpoint};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_stream_socket/async_write_some.html.
    // See also async_write().
    Sender_WriteSome async_write_some(std::span<char> data)
    {
        return Sender_WriteSome{_socket, data};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_stream_socket/async_read_some.html.
    // See also async_read().
    Sender_ReadSome async_read_some(std::span<char> buffer)
    {
        return Sender_ReadSome{_socket, buffer};
    }

    ~Async_TCPSocket()
    {
        disconnect();
    }

    void disconnect()
    {
        SOCKET socket = std::exchange(_socket, INVALID_SOCKET);
        if (socket == INVALID_SOCKET)
        {
            return;
        }
        int error = ::shutdown(socket, SD_BOTH);
        assert(error == 0);
        error = ::closesocket(socket);
        assert(error == 0);
        _iocp = nullptr;
    }

    Async_TCPSocket(const Async_TCPSocket&) = delete;
    Async_TCPSocket& operator=(const Async_TCPSocket&) = delete;
    Async_TCPSocket(Async_TCPSocket&& rhs) noexcept
        : _socket(std::exchange(rhs._socket, INVALID_SOCKET))
        , _iocp(std::exchange(rhs._iocp, nullptr))
    {
    }
    Async_TCPSocket& operator=(Async_TCPSocket&& rhs) noexcept
    {
        if (this != &rhs)
        {
            disconnect();
            _socket = std::exchange(rhs._socket, INVALID_SOCKET);
            _iocp = std::exchange(rhs._iocp, nullptr);
        }
        return *this;
    }
private:
    explicit Async_TCPSocket(SOCKET socket, wi::IoCompletionPort& iocp)
        : _socket(socket)
        , _iocp(&iocp)
    {
    }
};


// Manually implemented composition of async_write_some().
// See async_read() for implementation that uses generic algorithms to do the same thing.
template<typename Receiver>
struct Operation_Write : Operation_Log
{
    explicit Operation_Write(Receiver&& receiver, Async_TCPSocket& socket, std::span<char> data)
        : Operation_Log("write")
        , _receiver(std::move(receiver))
        , _socket(socket)
        , _data(data)
        , _write_some_op()
        , _written_bytes(0) { }

    struct Receiver_WriteSomePart
    {
        Operation_Write& _self;

        // #XXX: simple `set_value(std::size_t) && noexcept` does not compile. Why?
        friend void tag_invoke(unifex::tag_t<unifex::set_value>
            , Receiver_WriteSomePart&& self, std::size_t bytes_transferred) noexcept
        {
            self._self.on_wrote_part(bytes_transferred);
        }

        friend void tag_invoke(unifex::tag_t<unifex::set_error>
            , Receiver_WriteSomePart&& self, std::error_code ec) noexcept
        {
            self._self.on_wrote_part_error(ec);
        }

        friend void tag_invoke(unifex::tag_t<unifex::set_done>
            , Receiver_WriteSomePart&&) noexcept
        {
            assert(false);
        }

        // #XXX: does not compiles without this. MSVC bug?
        friend void tag_invoke(auto, Receiver_WriteSomePart&&, auto&&...) noexcept
        {
            static_assert(false);
            assert(false);
        }
    };

    Receiver _receiver;
    Async_TCPSocket& _socket;
    std::span<char> _data;
    std::size_t _written_bytes;

    using Op = Operation_WriteSome<Receiver_WriteSomePart>;
    using OpStorage = std::aligned_storage_t<sizeof(Op), alignof(Op)>;
    OpStorage _write_some_op;

    void do_write_rest(std::size_t to_send) noexcept
    {
        Op* op = new(static_cast<void*>(&_write_some_op)) Op(unifex::connect(
            _socket.async_write_some(_data.last(to_send))
                , Receiver_WriteSomePart{*this}));
        unifex::start(*op);
    }

    void on_wrote_part(std::size_t bytes_transferred) noexcept
    {
        _written_bytes += bytes_transferred;
        assert(_written_bytes <= _data.size());
        Op* op = static_cast<Op*>(static_cast<void*>(&_write_some_op));
        op->~Op();

        const std::size_t to_send = (_data.size() - _written_bytes);
        if (to_send > 0)
        {
            do_write_rest(to_send);
            return;
        }
        unifex::set_value(std::move(_receiver), _written_bytes);
    }

    void on_wrote_part_error(std::error_code ec) noexcept
    {
        unifex::set_error(std::move(_receiver), ec);
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_Write& self) noexcept
    {
        self.log("start");
        self.do_write_rest(self._data.size());
    }
};

struct Sender_Write : Sender_LogSimple<std::size_t, std::error_code>
{
    explicit Sender_Write(Async_TCPSocket& socket, std::span<char> buffer) noexcept
        : Sender_LogSimple<size_t, std::error_code>("write")
        , _socket(socket)
        , _buffer(buffer) { }

    Async_TCPSocket& _socket;
    std::span<char> _buffer;

    template<typename This, typename Receiver>
        requires std::is_same_v<Sender_Write, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_Write<Reveiver_>(std::move(receiver), self._socket, self._buffer);
    }
};

// #XXX: May crash with stack overflow. See async_read() with
// unifex::on(trampoline_scheduler(), ...) that fixes that.
static Sender_Write async_write(Async_TCPSocket& tcp_socket, std::span<char> data)
{
    return Sender_Write{tcp_socket, data};
}

template<typename Scheduler>
static auto async_read(Async_TCPSocket& tcp_socket, std::span<char> data, Scheduler scheduler)
{
    struct State
    {
        Async_TCPSocket& _socket;
        std::span<char> _data;
        std::size_t _read_bytes;
        Scheduler _scheduler;
    };

    return unifex::let_value(unifex::just(State{tcp_socket, data, 0, std::move(scheduler)})
        , [](State& state)
    {
        return unifex::repeat_effect_until(
            unifex::defer([&state]()
            {
                assert(state._read_bytes < state._data.size());
                const std::size_t remaining = (state._data.size() - state._read_bytes);
                // To avoid stackoverflow when async_read_some() completes immediately.
                return unifex::on(state._scheduler,
                    state._socket.async_read_some(state._data.last(remaining))
                        | unifex::then([&state](std::size_t bytes_transferred)
                        {
                            state._read_bytes += bytes_transferred;
                        }));
            })
            , [&state]()
            {
                return (state._read_bytes >= state._data.size());
            })
                | unifex::then([&state]() { return state._read_bytes; });
    });
}

struct StopReceiver
{
    bool& finish;

    friend void tag_invoke(auto, StopReceiver&& r, auto&&...) noexcept
    {
        r.finish = true;
    }
};

int main()
{
    WSADATA wsa_data{};
    int error = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    assert(error == 0);

    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    std::optional<Async_TCPSocket> socket = Async_TCPSocket::make(*iocp);
    assert(socket);

    auto endpoint = Endpoint_IPv4::from_string("127.0.0.1", 60260);
    assert(endpoint);

#define XX_MAYBE_TRIGGER_STACK_OVERFLOW() 0

#if (!XX_MAYBE_TRIGGER_STACK_OVERFLOW())
    char write_data[] = "Test";
    char read_data[sizeof(write_data)]{};
#else
    // Send a lot of data to echo server, local host.
    // That most likely leads to some sockets reads operations
    // to complete immediately.
    std::vector<char> write_data;
    write_data.resize(1 * 1024 * 1024 * 1024, 'x');
    std::vector<char> read_data;
    read_data.resize(write_data.size());
#endif

#if (XX_MAYBE_TRIGGER_STACK_OVERFLOW())
    unifex::trampoline_scheduler scheduler(128/*max depth*/);
#else
    // Stackoverflow may be in async_read().
    unifex::inline_scheduler scheduler;
#endif

    // RUN python.exe tpc_server.py.
    auto logic = [&]()
    {
        return unifex::sequence(
              socket->async_connect(*endpoint)
                | unifex::then([](void_)
                {
                    printf("Connected!.\n");
                })
            , async_write(*socket, write_data)
                    | unifex::then([](std::size_t bytes_transferred)
                {
                    printf("Sent %i bytes!.\n", int(bytes_transferred));
                })
            , async_read(*socket, read_data, scheduler)
                    | unifex::then([&](std::size_t bytes_transferred)
                {
                    printf("Received %i bytes.\n", int(bytes_transferred));
                })
            );
    };

    bool finish = false;
    auto state = unifex::connect(logic(), StopReceiver{finish});
    unifex::start(state);

    while (!finish)
    {
        wi::PortEntry entries[4];
        for (const wi::PortEntry& entry : iocp->get_many(entries, ec))
        {
            IOCP_Overlapped* ov = static_cast<IOCP_Overlapped*>(entry.overlapped);
            assert(ov);
            assert(ov->_callback);
            ov->_callback(ov->_user_data, entry, ec);
        }
    }

    socket->disconnect();
    WSACleanup();
}
