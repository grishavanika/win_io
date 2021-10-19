#include <WinSock2.h>
#include <WS2tcpip.h>

#if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#if !defined(_WINSOCKAPI_)
#  define _WINSOCKAPI_
#endif
#include <MSWSock.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

#include <win_io/detail/io_completion_port.h>

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

#include <exception> // Not needed. For libunifex.

#include <system_error>
#include <optional>
#include <utility>
#include <type_traits>
#include <span>
#include <limits>
#include <variant>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

#define XX_ENABLE_OPERATION_LOGS() 0
#define XX_ENABLE_SENDERS_LOGS() 0

static constexpr wi::WinULONG_PTR kClientKeyIOCP = 1;

struct IOCP_Overlapped : OVERLAPPED
{
    LPOVERLAPPED ptr() { return this; }

    using Handle = void (*)(void* user_data, const wi::PortEntry& /*entry*/, std::error_code /*ec*/);
    Handle _callback = nullptr;
    void* _user_data = nullptr;
};

// Hack to make set_void() (no T) work with unifex::then(). Bug?
using void_ = struct {};

struct Operation_Log
{
    Operation_Log(const Operation_Log&) = delete;
    Operation_Log& operator=(const Operation_Log&) = delete;
    Operation_Log& operator=(Operation_Log&&) = delete;
    Operation_Log(Operation_Log&&) = delete;

protected:
    ~Operation_Log() noexcept = default;
#if (XX_ENABLE_OPERATION_LOGS())
    const char* _name = nullptr;
    explicit Operation_Log(const char* name) noexcept
        : _name(name) { }
    void log([[maybe_unused]] const char* debug)
    {
        printf("[State ][%p] '%s' - %s.\n", static_cast<void*>(this), _name, debug);
    }
#else
    explicit Operation_Log(const char*) noexcept { }
    void log(const char*) { }
#endif
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
    ~Sender_LogSimple() noexcept = default;
#if (XX_ENABLE_SENDERS_LOGS())
    const char* _name = nullptr;
    explicit Sender_LogSimple(const char* name) noexcept
        : _name(name) {}
    void log(const char* debug)
    {
        printf("[Sender][%p] '%s' - %s.\n", static_cast<void*>(this), _name, debug);
    }
#else
    explicit Sender_LogSimple(const char*) noexcept {}
    void log(const char*) { }
#endif
};

struct Endpoint_IPv4
{
    std::uint16_t _port_network = 0;
    std::uint32_t _ip_network = 0;

    // https://man7.org/linux/man-pages/man3/inet_pton.3.html
    // `src` is in dotted-decimal format, "ddd.ddd.ddd.ddd".
    static std::optional<Endpoint_IPv4> from_string(const char* src, std::uint16_t port_host)
    {
        struct in_addr ipv4 {};
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
    explicit Operation_Connect(Receiver&& receiver, SOCKET socket, Endpoint_IPv4 endpoint) noexcept
        : Operation_Log("connect")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_Connect::on_connected, this}
        , _socket(socket)
        , _endpoint(endpoint) {}

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    Endpoint_IPv4 _endpoint;

    static void on_connected(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_Connect& self = *static_cast<Operation_Connect*>(user_data);
        self.log("on_connected");

        assert(entry.bytes_transferred == 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == self._ov.ptr());

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
            struct sockaddr_in local_address {};
            local_address.sin_family = AF_INET;
            local_address.sin_addr.s_addr = INADDR_ANY;
            local_address.sin_port = 0;
            const int error = ::bind(self._socket
                , reinterpret_cast<SOCKADDR*>(&local_address)
                , sizeof(local_address));
            assert(error == 0);
        }

        struct sockaddr_in connect_to {};
        connect_to.sin_family = AF_INET;
        connect_to.sin_port = self._endpoint._port_network;
        connect_to.sin_addr.s_addr = self._endpoint._ip_network;

        const BOOL finished = ConnectEx(self._socket
            , (sockaddr*)&connect_to
            , sizeof(connect_to)
            , nullptr
            , 0
            , nullptr
            , self._ov.ptr());
        if (finished)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_connected(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
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
    explicit Operation_WriteSome(Receiver&& receiver, SOCKET socket, std::span<char> data) noexcept
        : Operation_Log("write_some")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_WriteSome::on_sent, this}
        , _socket(socket)
        , _data(data) { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _data;

    static void on_sent(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_WriteSome& self = *static_cast<Operation_WriteSome*>(user_data);
        self.log("on_sent");

        assert(entry.bytes_transferred > 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == self._ov.ptr());

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
        assert(self._data.size() <= (std::numeric_limits<ULONG>::max)());

        WSABUF send_buffer{};
        send_buffer.buf = self._data.data();
        send_buffer.len = ULONG(self._data.size());
        const int error = ::WSASend(self._socket
            , &send_buffer, 1
            , nullptr/*bytes_send*/
            , 0/*flags*/
            , self._ov.ptr()
            , nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = wi::WinDWORD(self._data.size());
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_sent(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_sent(&self, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

struct Sender_WriteSome : Sender_LogSimple<std::size_t, std::error_code>
{
    explicit Sender_WriteSome(SOCKET socket, std::span<char> data) noexcept
        : Sender_LogSimple<std::size_t, std::error_code>("write_some")
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
    explicit Operation_ReadSome(Receiver&& receiver, SOCKET socket, std::span<char> buffer) noexcept
        : Operation_Log("read_some")
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

    static void on_received(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_ReadSome& self = *static_cast<Operation_ReadSome*>(user_data);
        self.log("on_received");

        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == self._ov.ptr());

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
            , self._ov.ptr()
            , nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = received;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_received(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_received(&self, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

struct Sender_ReadSome : Sender_LogSimple<std::size_t, std::error_code>
{
    explicit Sender_ReadSome(SOCKET socket, std::span<char> buffer) noexcept
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

template<typename Receiver
    , typename _Async_TCPSocket /*= Async_TCPSocket*/>
    struct Operation_Accept : Operation_Log
{
    explicit Operation_Accept(Receiver&& receiver, SOCKET listen_socket, wi::IoCompletionPort& iocp) noexcept
        : Operation_Log("accept")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_Accept::on_accepted, this}
        , _listen_socket(listen_socket)
        , _iocp(&iocp)
        , _buffer{}
        , _client{} { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _listen_socket = INVALID_SOCKET;
    wi::IoCompletionPort* _iocp = nullptr;

    // As per AcceptEx() and GetAcceptExSockaddrs():
    // https://docs.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-acceptex
    // https://docs.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-getacceptexsockaddrs.
    static constexpr std::size_t kAddressLength = (sizeof(struct sockaddr) + 16);
    char _buffer[2 * kAddressLength]{};
    _Async_TCPSocket _client;

    static void on_accepted(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_Accept& self = *static_cast<Operation_Accept*>(user_data);
        self.log("on_accepted");

        assert(entry.bytes_transferred == 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == self._ov.ptr());

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

#if (0)
        ::GetAcceptExSockaddrs(self._buffer
            , 0
            , Operation_Accept::kAddressLength
            , Operation_Accept::kAddressLength
            , )
#endif

        const int error = ::setsockopt(self._client._socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT
            , reinterpret_cast<char*>(&self._listen_socket)
            , sizeof(self._listen_socket));
        assert(error == 0);
        // Close socket on error?

        unifex::set_value(std::move(self._receiver), std::move(self._client));
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_Accept& self) noexcept
    {
        self.log("start");

        std::error_code ec;
        auto client = _Async_TCPSocket::make(*self._iocp, ec);
        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }
        assert(client);
        self._client = std::move(*client);

        using Self = Operation_Accept<Receiver, _Async_TCPSocket>;
        const DWORD address_length = DWORD(Self::kAddressLength);
        DWORD bytes_received = 0;
        const BOOL ok = ::AcceptEx(self._listen_socket, self._client._socket
            , self._buffer
            , 0
            , address_length
            , address_length
            , &bytes_received
            , self._ov.ptr());
        if (ok == TRUE)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = bytes_received;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_accepted(&self, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = self._ov.ptr();
            self.on_accepted(&self, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

template<typename _Async_TCPSocket /*= Async_TCPSocket*/>
struct Sender_Accept : Sender_LogSimple<_Async_TCPSocket, std::error_code>
{
    explicit Sender_Accept(SOCKET listen_socket, wi::IoCompletionPort& iocp) noexcept
        : Sender_LogSimple<_Async_TCPSocket, std::error_code>("accept")
        , _listen_socket(listen_socket)
        , _iocp(&iocp) { }

    SOCKET _listen_socket;
    wi::IoCompletionPort* _iocp = nullptr;

    template<typename This, typename Receiver>
        requires std::is_same_v<Sender_Accept, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_Accept<Reveiver_, _Async_TCPSocket>(std::move(receiver), self._listen_socket, *self._iocp);
    }
};

struct Async_TCPSocket
{
    SOCKET _socket = INVALID_SOCKET;
    wi::IoCompletionPort* _iocp = nullptr;

    explicit Async_TCPSocket() noexcept = default;

    static std::optional<Async_TCPSocket> make(wi::IoCompletionPort& iocp, std::error_code& ec) noexcept
    {
        ec = std::error_code();
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
            ec = std::error_code(::WSAGetLastError(), std::system_category());
            return std::nullopt;
        }

        const BOOL ok = ::SetFileCompletionNotificationModes(HANDLE(handle)
            , FILE_SKIP_SET_EVENT_ON_HANDLE
            | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
        if (!ok)
        {
            ec = std::error_code(::GetLastError(), std::system_category());
            return std::nullopt;
        }

        u_long flags = 1; // non-blocking, enable.
        const int error = ::ioctlsocket(handle, FIONBIO, &flags);
        if (error)
        {
            ec = std::error_code(::WSAGetLastError(), std::system_category());
            return std::nullopt;
        }

        iocp.associate_socket(wi::WinSOCKET(handle), kClientKeyIOCP, ec);
        if (ec)
        {
            return std::nullopt;
        }
        return std::make_optional(std::move(socket));
    }

    Sender_Connect async_connect(Endpoint_IPv4 endpoint) noexcept
    {
        return Sender_Connect{_socket, endpoint};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_socket_acceptor/async_accept/overload3.html.
    Sender_Accept<Async_TCPSocket> async_accept() noexcept
    {
        return Sender_Accept<Async_TCPSocket>{_socket, * _iocp};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_stream_socket/async_write_some.html.
    // See also async_write().
    Sender_WriteSome async_write_some(std::span<char> data) noexcept
    {
        return Sender_WriteSome{_socket, data};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_stream_socket/async_read_some.html.
    // See also async_read().
    Sender_ReadSome async_read_some(std::span<char> buffer) noexcept
    {
        return Sender_ReadSome{_socket, buffer};
    }

    ~Async_TCPSocket() noexcept
    {
        disconnect();
    }

    void disconnect() noexcept
    {
        SOCKET socket = std::exchange(_socket, INVALID_SOCKET);
        if (socket == INVALID_SOCKET)
        {
            return;
        }
        int error = ::shutdown(socket, SD_BOTH);
        (void)error;
        error = ::closesocket(socket);
        (void)error;
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
    explicit Async_TCPSocket(SOCKET socket, wi::IoCompletionPort& iocp) noexcept
        : _socket(socket)
        , _iocp(&iocp)
    {
    }
};

template<typename Receiver>
struct Operation_IOCP_Schedule : Operation_Log
{
    static constexpr wi::WinULONG_PTR kScheduleKeyIOCP = 2;

    explicit Operation_IOCP_Schedule(Receiver&& receiver, wi::IoCompletionPort& iocp) noexcept
        : Operation_Log("iocp_schedule")
        , _receiver(std::move(receiver))
        , _ov{{}, &Operation_IOCP_Schedule::on_scheduled, this}
        , _iocp(&iocp) { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    wi::IoCompletionPort* _iocp = nullptr;

    static void on_scheduled(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_IOCP_Schedule& self = *static_cast<Operation_IOCP_Schedule*>(user_data);
        self.log("on_scheduled");

        assert(entry.bytes_transferred == 0);
        assert(entry.completion_key == kScheduleKeyIOCP);
        assert(entry.overlapped == self._ov.ptr());

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

        unifex::set_value(std::move(self._receiver));
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_IOCP_Schedule& self) noexcept
    {
        self.log("start");
        wi::PortEntry entry{};
        entry.bytes_transferred = 0;
        entry.completion_key = kScheduleKeyIOCP;
        entry.overlapped = self._ov.ptr();
        std::error_code ec;
        self._iocp->post(entry, ec);
        if (ec)
        {
            self.on_scheduled(&self, entry, ec);
        }
    }
};

struct Sender_IOCP_Schedule : Sender_LogSimple<void, std::error_code>
{
    explicit Sender_IOCP_Schedule(wi::IoCompletionPort& iocp) noexcept
        : Sender_LogSimple<void, std::error_code>("iocp_schedule")
        , _iocp(&iocp) { }

    wi::IoCompletionPort* _iocp;

    template<typename This, typename Receiver>
        requires std::is_same_v<Sender_IOCP_Schedule, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_IOCP_Schedule<Reveiver_>(std::move(receiver), *self._iocp);
    }
};

static Sender_IOCP_Schedule IOCP_schedule(wi::IoCompletionPort& iocp)
{
    return Sender_IOCP_Schedule{iocp};
}

struct IOCP_Scheduler
{
    wi::IoCompletionPort* _iocp = nullptr;
    Sender_IOCP_Schedule schedule() noexcept { return IOCP_schedule(*_iocp); }
    bool operator==(const IOCP_Scheduler& rhs) const { return (_iocp == rhs._iocp); }
    bool operator!=(const IOCP_Scheduler& rhs) const { return (_iocp != rhs._iocp); }
};

static_assert(unifex::scheduler<IOCP_Scheduler>);

template<typename Receiver, typename Scheduler, typename Fallback>
struct Operation_SelectOnceInN : Operation_Log
{
    explicit Operation_SelectOnceInN(Receiver&& receiver, unsigned* counter, unsigned N, Scheduler scheduler, Fallback fallback) noexcept
        : Operation_Log("select_once_in_n")
        , _receiver(std::move(receiver))
        , _counter(counter)
        , _N(N)
        , _scheduler(std::move(scheduler))
        , _fallback(fallback)
        , _op{} { }

    Receiver _receiver;
    unsigned* _counter;
    unsigned _N;
    Scheduler _scheduler; // no_unique_address
    Fallback _fallback; // no_unique_address

    struct OnScheduledReceiver
    {
        Operation_SelectOnceInN* _self = nullptr;

        // #XXX: should be generic of terms Scheduler & Fallback.
        void set_value() noexcept
        {
            _self->destroy_op();
            unifex::set_value(std::move(_self->_receiver));
        }
        void set_error(auto&& e1) noexcept
        {
            _self->destroy_op();
            if constexpr (requires { unifex::set_value(std::move(_self->_receiver), e1); })
            {
                unifex::set_value(std::move(_self->_receiver), e1);
            }
            else if constexpr (requires { unifex::set_value(std::move(_self->_receiver)); })
            {
                unifex::set_value(std::move(_self->_receiver));
            }
            else
            {
                static_assert(sizeof(Receiver) == 0
                    , "This temporary code is missing generic handling of Receiver.");
            }
        }
        void set_done() noexcept
        {
            _self->destroy_op();
            unifex::set_done(std::move(_self->_receiver));
        }
    };

    using Op_Scheduler = decltype(unifex::connect(
        std::declval<Scheduler&>().schedule()
        , std::declval<OnScheduledReceiver>()));
    using Op_Fallback = decltype(unifex::connect(
        std::declval<Fallback&>().schedule()
        , std::declval<OnScheduledReceiver>()));
    using Storage_Scheduler = std::aligned_storage_t<sizeof(Op_Scheduler), alignof(Op_Scheduler)>;
    using Storage_Fallback = std::aligned_storage_t<sizeof(Op_Fallback), alignof(Op_Fallback)>;

    using Op_State = std::variant<
        std::monostate
        , Storage_Scheduler
        , Storage_Fallback>;

    Op_State _op;

    void destroy_op()
    {
        if (void* scheduler = std::get_if<1>(&_op))
        {
            auto* op = static_cast<Op_Scheduler*>(scheduler);
            op->~Op_Scheduler();
        }
        else if (void* fallback = std::get_if<2>(&_op))
        {
            auto* op = static_cast<Op_Fallback*>(fallback);
            op->~Op_Fallback();
        }
        else
        {
            assert(false);
        }
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, Operation_SelectOnceInN& self) noexcept
    {
        assert(self._counter);
        assert(self._N > 0);
        const unsigned counter = ++(*self._counter);
        if ((counter % self._N) == 0)
        {
            // Select & run Scheduler.
            void* storage = &self._op.template emplace<1>();
            auto* op = new(storage) auto(unifex::connect(
                self._scheduler.schedule()
                , OnScheduledReceiver{&self}));
            unifex::start(*op);
        }
        else
        {
            // Select & run Fallback Scheduler.
            void* storage = &self._op.template emplace<2>();
            auto* op = new(storage) auto(unifex::connect(
                self._fallback.schedule()
                , OnScheduledReceiver{&self}));
            unifex::start(*op);
        }
    }
};

// #XXX: error should be variant<> from both schedulers.
template<typename Scheduler, typename Fallback>
struct Sender_SelectOnceInN : Sender_LogSimple<void, void>
{
    explicit Sender_SelectOnceInN(unsigned* counter, unsigned N, Scheduler scheduler, Fallback fallback) noexcept
        : Sender_LogSimple<void, void>("select_once_in_n")
        , _counter(counter)
        , _N(N)
        , _scheduler(std::move(scheduler))
        , _fallback(fallback) { }

    unsigned* _counter;
    unsigned _N;
    Scheduler _scheduler; // no_unique_address
    Fallback _fallback; // no_unique_address

    template<typename This, typename Receiver>
        requires std::is_same_v<Sender_SelectOnceInN, std::remove_cvref_t<This>>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , This&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return Operation_SelectOnceInN<Reveiver_, Scheduler, Fallback>(std::move(receiver)
            , self._counter, self._N, self._scheduler, self._fallback);
    }
};

template<typename Scheduler, typename Fallback = unifex::inline_scheduler>
struct SelectOnceInN_Scheduler
{
    unsigned* _counter;
    unsigned _N;
    Scheduler _scheduler; // no_unique_address
    Fallback _fallback; // no_unique_address

    explicit SelectOnceInN_Scheduler(Scheduler scheduler, unsigned& counter_now, unsigned n = 128, Fallback fallback = Fallback{}) noexcept
        : _counter(&counter_now)
        , _N(n)
        , _scheduler(std::move(scheduler))
        , _fallback(std::move(fallback)) {}

    bool operator==(const SelectOnceInN_Scheduler& rhs) const
    {
        return (_counter == rhs._counter)
            && (_N == rhs._N)
            && (_scheduler == rhs._scheduler)
            && (_fallback == rhs._fallback);
    }
    bool operator!=(const SelectOnceInN_Scheduler& rhs) const { return !(*this == rhs); }
    
    Sender_SelectOnceInN<Scheduler, Fallback> schedule() noexcept
    {
        return Sender_SelectOnceInN<Scheduler, Fallback>{_counter, _N, _scheduler, _fallback};
    }
};

static_assert(unifex::scheduler<SelectOnceInN_Scheduler<unifex::inline_scheduler>>);
static_assert(unifex::scheduler<SelectOnceInN_Scheduler<IOCP_Scheduler>>);

static auto async_read(Async_TCPSocket& tcp_socket, std::span<char> data) noexcept
{
    struct State
    {
        Async_TCPSocket& _socket;
        std::span<char> _data{};
        std::size_t _read_bytes = 0;
        unsigned _recursion_counter = 0;

        auto scheduler()
        {
            // Schedule once in 128 calls into IOCP, otherwise inline_sheduler.
            const unsigned unroll_depth = 128;
            return SelectOnceInN_Scheduler<IOCP_Scheduler>(
                IOCP_Scheduler{_socket._iocp}
                , _recursion_counter
                , unroll_depth);
        }
    };

    return unifex::let_value(unifex::just(State{tcp_socket, data, 0, 0})
        , [](State& state)
    {
        return unifex::repeat_effect_until(
            unifex::defer([&state]()
            {
                assert(state._read_bytes < state._data.size());
                const std::size_t remaining = (state._data.size() - state._read_bytes);
                return unifex::on(state.scheduler(),
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
                | unifex::then([&state]()
            {
#if (0)
                printf("Read %zu bytes with %u calls.\n", state._read_bytes, state._recursion_counter);
#endif
                return state._read_bytes;
            });
    });
}

// Same as async_read().
static auto async_write(Async_TCPSocket& tcp_socket, std::span<char> data) noexcept
{
    struct State
    {
        Async_TCPSocket& _socket;
        std::span<char> _data{};
        std::size_t _written_bytes = 0;
        unsigned _recursion_counter = 0;

        auto scheduler()
        {
            const unsigned unroll_depth = 128;
            return SelectOnceInN_Scheduler<IOCP_Scheduler>(
                IOCP_Scheduler{_socket._iocp}
                , _recursion_counter
                , unroll_depth);
        }
    };

    return unifex::let_value(unifex::just(State{tcp_socket, data, 0, 0})
        , [](State& state)
    {
        return unifex::repeat_effect_until(
            unifex::defer([&state]()
            {
                assert(state._written_bytes < state._data.size());
                const std::size_t remaining = (state._data.size() - state._written_bytes);
                return unifex::on(state.scheduler(),
                    state._socket.async_write_some(state._data.last(remaining))
                        | unifex::then([&state](std::size_t bytes_transferred)
                    {
                        state._written_bytes += bytes_transferred;
                    }));
            })
            , [&state]()
            {
                return (state._written_bytes >= state._data.size());
            })
                | unifex::then([&state]()
            {
#if (0)
                printf("Wrote %zu bytes with %u calls.\n", state._written_bytes, state._recursion_counter);
#endif
                return state._written_bytes;
            });
    });
}

struct StopReceiver
{
    bool& _finish;

    friend void tag_invoke(auto, StopReceiver&& self, auto&&...) noexcept
    {
        self._finish = true;
    }
};

#include <vector>
#include <chrono>

#include <cstring>

int main()
{
    WSADATA wsa_data{};
    int error = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    assert(error == 0);

    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    std::optional<Async_TCPSocket> client_socket = Async_TCPSocket::make(*iocp, ec);
    assert(!ec);
    assert(client_socket);

    std::optional<Async_TCPSocket> server_socket = Async_TCPSocket::make(*iocp, ec);
    assert(!ec);
    assert(server_socket);

    auto endpoint = Endpoint_IPv4::from_string("127.0.0.1", 60260);
    assert(endpoint);

    std::vector<char> write_data;
    write_data.resize(1ull * 1024 * 1024 * 1024 * 1, 'x');
    std::vector<char> read_data;
    read_data.resize(write_data.size());

    std::vector<char> server_data;
    server_data.resize(write_data.size());

    // https://docs.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse.
    int enable_reuse = 1;
    error = ::setsockopt(server_socket->_socket, SOL_SOCKET, SO_REUSEADDR
        , reinterpret_cast<char*>(&enable_reuse), sizeof(enable_reuse));
    assert(error == 0);

    struct sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = endpoint->_ip_network;
    server_address.sin_port = endpoint->_port_network;
    error = ::bind(server_socket->_socket
        , reinterpret_cast<SOCKADDR*>(&server_address)
        , sizeof(server_address));
    assert(error == 0);

    error = ::listen(server_socket->_socket, 16);
    assert(error == 0);

    struct ServerState
    {
        Async_TCPSocket _client;
    };

    auto server_logic = [&]()
    {
        return unifex::let_value(unifex::just(ServerState())
            , [&](ServerState& state)
        {
            return unifex::sequence(
                server_socket->async_accept()
                    | unifex::then([&state](Async_TCPSocket client)
                {
                    state._client = std::move(client);
                    printf("[Server] Accepted.\n");
                })
                , async_read(state._client, server_data)
                    | unifex::then([&](std::size_t bytes_transferred)
                {
                    printf("[Server] Received %zu bytes.\n", bytes_transferred);
                })
                , async_write(state._client, server_data)
                    | unifex::then([&state](std::size_t bytes_transferred)
                {
                    printf("[Server] Sent %zu bytes.\n", bytes_transferred);
                    state._client.disconnect();
                    (void)state;
                })
                );
        });
    };

    auto client_logic = [&]()
    {
        return unifex::sequence(
            client_socket->async_connect(*endpoint)
                | unifex::then([](void_)
            {
                printf("[Client] Connected.\n");
            })
            , async_write(*client_socket, write_data)
                | unifex::then([](std::size_t bytes_transferred)
            {
                printf("[Client] Sent %zu bytes.\n", bytes_transferred);
            })
            , async_read(*client_socket, read_data)
                | unifex::then([&](std::size_t bytes_transferred)
            {
                printf("[Client] Received %zu bytes.\n", bytes_transferred);
            })
            );
    };

    const auto start = std::chrono::steady_clock::now();

    bool finish_server = false;
    auto server_state = unifex::connect(server_logic(), StopReceiver{finish_server});
    unifex::start(server_state);

    bool finish_client = false;
    auto client_state = unifex::connect(client_logic(), StopReceiver{finish_client});
    unifex::start(client_state);

    while (!finish_client || !finish_server)
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

    const auto end = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration_cast<std::chrono::duration<float>>(end - start).count();
    printf("Elapsed: %.3f seconds.\n", seconds);

    assert(memcmp(write_data.data(), read_data.data(), write_data.size()) == 0);
    assert(memcmp(write_data.data(), server_data.data(), write_data.size()) == 0);

    client_socket->disconnect();
    server_socket->disconnect();

    ::WSACleanup();
}
