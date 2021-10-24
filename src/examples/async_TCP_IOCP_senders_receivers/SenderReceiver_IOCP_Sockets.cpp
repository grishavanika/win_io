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
#include <unifex/inline_scheduler.hpp>
#include <unifex/on.hpp>
#include <unifex/when_all.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/manual_lifetime.hpp>

#include <system_error>
#include <optional>
#include <utility>
#include <type_traits>
#include <span>
#include <limits>
#include <variant>
#include <string_view>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

// Few helpers to simplify "standard" definitions of Senders
// (one overload of set_value() and set_error()) and
// Operation states (e.g., no copies are allowed). Mostly
// to ensure this is true for learning purpose.

template<typename Sender>
using XX_Values = unifex::sender_value_types_t<Sender, std::variant, std::tuple>;
template<typename Sender>
using XX_Errors = unifex::sender_error_types_t<Sender, std::variant>;

template<typename Unknown>
struct XX_Show;

struct MoveOnly
{
    MoveOnly() noexcept = default;
    ~MoveOnly() noexcept = default;

    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&&) = delete;
    // Needed.
    MoveOnly(MoveOnly&&) noexcept = default;
};

struct Operation_Base : MoveOnly
{
    // Let say this is unmovable. See when this may be needed.
    // (E.g. move *before* operation's start()).
    Operation_Base(Operation_Base&&) = delete;
    Operation_Base() noexcept = default;
};

template<typename V, typename E>
struct Sender_LogSimple : MoveOnly
{
    template<template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<V>>;
    template <template <typename...> class Variant>
    using error_types = Variant<E>;
    static constexpr bool sends_done = true;
};

template<typename E>
struct Sender_LogSimple<void, E> : MoveOnly
{
    template<template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;
                        //      ^^^^^^^ void
    template <template <typename...> class Variant>
    using error_types = Variant<E>;
    static constexpr bool sends_done = true;
};

template<typename V>
struct Sender_LogSimple<V, void> : MoveOnly
{
    template<template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<V>>;
    template <template <typename...> class Variant>
    using error_types = Variant<>;
                        // ^^^^^^^ void
    static constexpr bool sends_done = true;
};

template<>
struct Sender_LogSimple<void, void> : MoveOnly
{
    template<template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;
                        //      ^^^^^^^ void
    template <template <typename...> class Variant>
    using error_types = Variant<>;
                        // ^^^^^^^ void
    static constexpr bool sends_done = true;
};

// WSA.
struct Initialize_WSA
{
    explicit Initialize_WSA()
    {
        WSADATA wsa_data{};
        const int error = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
        assert(error == 0);
    }
    ~Initialize_WSA() noexcept
    {
        const int error = ::WSACleanup();
        assert(error == 0);
    }
    Initialize_WSA(const Initialize_WSA&) = delete;
    Initialize_WSA& operator=(const Initialize_WSA&) = delete;
    Initialize_WSA(Initialize_WSA&&) = delete;
    Initialize_WSA& operator=(Initialize_WSA&&) = delete;
};

static constexpr wi::WinULONG_PTR kClientKeyIOCP = 1;

struct IOCP_Overlapped : OVERLAPPED
{
    LPOVERLAPPED ptr() { return this; }

    using Handle = void (*)(void* user_data, const wi::PortEntry& /*entry*/, std::error_code /*ec*/);
    Handle _callback = nullptr;
    void* _user_data = nullptr;
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
        const int ok = ::inet_pton(AF_INET, src, &ipv4);
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

template<typename Receiver
    , typename StopToken = unifex::stop_token_type_t<Receiver>>
struct Operation_Connect : Operation_Base
{
    struct Callback_Stop
    {
        Operation_Connect* _self = nullptr;
        void operator()() noexcept { _self->try_stop(); }
    };
    using StopCallback = typename StopToken::template callback_type<Callback_Stop>;

    Receiver _receiver;
    SOCKET _socket = INVALID_SOCKET;
    Endpoint_IPv4 _endpoint;
    IOCP_Overlapped _ov{{}, &Operation_Connect::on_connected, this};
    unifex::manual_lifetime<StopCallback> _stop_callback{};

    void try_stop() noexcept
    {
        HANDLE handle = reinterpret_cast<HANDLE>(_socket);
        const BOOL ok = ::CancelIoEx(handle, _ov.ptr());
        if (!ok)
        {
            const DWORD error = GetLastError();
            // No request to cancel. Finished?
            assert(error == ERROR_NOT_FOUND);
        }
    }

    static void on_connected(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_Connect& self = *static_cast<Operation_Connect*>(user_data);
        assert(entry.bytes_transferred == 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == self._ov.ptr());

        if constexpr (!unifex::is_stop_never_possible_v<StopToken>)
        {
            self._stop_callback.destruct();
            auto stop_token = unifex::get_stop_token(self._receiver);
            if (stop_token.stop_requested())
            {
                unifex::set_done(std::move(self._receiver));
                return;
            }
        }

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

        const int ok = ::setsockopt(self._socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
        if (ok != 0)
        {
            const int wsa_error = ::WSAGetLastError();
            unifex::set_error(std::move(self._receiver)
                , std::error_code(wsa_error, std::system_category()));
            return;
        }

        unifex::set_value(std::move(self._receiver));
    }

    void start() & noexcept
    {
        LPFN_CONNECTEX ConnectEx = nullptr;
        {
            GUID GuidConnectEx = WSAID_CONNECTEX;
            DWORD bytes = 0;
            const int error = ::WSAIoctl(_socket
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
            int ok = ::bind(_socket
                , reinterpret_cast<SOCKADDR*>(&local_address)
                , sizeof(local_address));
            const int wsa_error = ::WSAGetLastError();
            assert((ok == 0)
                // WSAEINVAL - when binding a socked 2nd or more time :(
                || (wsa_error == WSAEINVAL));
        }

        if constexpr (!unifex::is_stop_never_possible_v<StopToken>)
        {
            auto stop_token = unifex::get_stop_token(_receiver);
            _stop_callback.construct(stop_token, Callback_Stop{this});
        }

        struct sockaddr_in connect_to {};
        connect_to.sin_family = AF_INET;
        connect_to.sin_port = _endpoint._port_network;
        connect_to.sin_addr.s_addr = _endpoint._ip_network;
        const BOOL finished = ConnectEx(_socket
            , (sockaddr*)&connect_to
            , sizeof(connect_to)
            , nullptr
            , 0
            , nullptr
            , _ov.ptr());
        if (finished)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_connected(this, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_connected(this, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

struct Sender_Connect : Sender_LogSimple<void, std::error_code>
{
    SOCKET _socket = INVALID_SOCKET;
    Endpoint_IPv4 _endpoint;

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_Connect<Receiver_>{{}
            , std::move(receiver), _socket, _endpoint};
    }
};

template<typename Receiver>
struct Operation_WriteSome : Operation_Base
{
    Receiver _receiver;
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _data;
    IOCP_Overlapped _ov{{}, &Operation_WriteSome::on_sent, this};

    static void on_sent(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_WriteSome& self = *static_cast<Operation_WriteSome*>(user_data);
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

    void start() & noexcept
    {
        assert(_data.size() > 0);
        assert(_data.size() <= (std::numeric_limits<ULONG>::max)());

        WSABUF send_buffer{};
        send_buffer.buf = _data.data();
        send_buffer.len = ULONG(_data.size());
        const int error = ::WSASend(_socket
            , &send_buffer, 1
            , nullptr/*bytes_send*/
            , 0/*flags*/
            , _ov.ptr()
            , nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = wi::WinDWORD(_data.size());
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_sent(this, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_sent(this, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

struct Sender_WriteSome : Sender_LogSimple<std::size_t, std::error_code>
{
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _data;

    template <typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_WriteSome<Receiver_>{{}, std::move(receiver), _socket, _data};
    }
};

template<typename Receiver>
struct Operation_ReadSome : Operation_Base
{
    Receiver _receiver;
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _buffer;
    IOCP_Overlapped _ov{{}, &Operation_ReadSome::on_received, this};
    DWORD _flags = 0;

    static void on_received(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_ReadSome& self = *static_cast<Operation_ReadSome*>(user_data);
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

    void start() & noexcept
    {
        assert(_buffer.size() > 0);

        WSABUF receive_buffer{};
        receive_buffer.buf = _buffer.data();
        receive_buffer.len = ULONG(_buffer.size());
        _flags = MSG_PARTIAL;
        DWORD received = 0;
        const int error = ::WSARecv(_socket
            , &receive_buffer, 1
            , &received
            , &_flags
            , _ov.ptr()
            , nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = received;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_received(this, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_received(this, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

struct Sender_ReadSome : Sender_LogSimple<std::size_t, std::error_code>
{
    SOCKET _socket;
    std::span<char> _buffer;

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_ReadSome<Receiver_>{{}, std::move(receiver), _socket, _buffer};
    }
};

template<typename Receiver
    , typename _Async_TCPSocket /*= Async_TCPSocket*/>
    struct Operation_Accept : Operation_Base
{
    // As per AcceptEx() and GetAcceptExSockaddrs():
    // https://docs.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-acceptex
    // https://docs.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-getacceptexsockaddrs.
    static constexpr std::size_t kAddressLength = (sizeof(struct sockaddr) + 16);

    Receiver _receiver;
    SOCKET _listen_socket = INVALID_SOCKET;
    wi::IoCompletionPort* _iocp = nullptr;
    IOCP_Overlapped _ov{{}, &Operation_Accept::on_accepted, this};
    char _buffer[2 * kAddressLength]{};
    _Async_TCPSocket _client{};

    static void on_accepted(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_Accept& self = *static_cast<Operation_Accept*>(user_data);
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
        // Close socket on error.

        unifex::set_value(std::move(self._receiver), std::move(self._client));
    }

    void start() & noexcept
    {
        std::error_code ec;
        auto client = _Async_TCPSocket::make(*_iocp, ec);
        if (ec)
        {
            unifex::set_error(std::move(_receiver), ec);
            return;
        }
        assert(client);
        _client = std::move(*client);

        using Self = Operation_Accept<Receiver, _Async_TCPSocket>;
        const DWORD address_length = DWORD(Self::kAddressLength);
        DWORD bytes_received = 0;
        const BOOL ok = ::AcceptEx(_listen_socket, _client._socket
            , _buffer
            , 0
            , address_length
            , address_length
            , &bytes_received
            , _ov.ptr());
        if (ok == TRUE)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = bytes_received;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_accepted(this, entry, std::error_code());
            return;
        }
        const int wsa_error = ::WSAGetLastError();
        if (wsa_error != ERROR_IO_PENDING)
        {
            wi::PortEntry entry;
            entry.bytes_transferred = 0;
            entry.completion_key = kClientKeyIOCP;
            entry.overlapped = _ov.ptr();
            on_accepted(this, entry
                , std::error_code(wsa_error, std::system_category()));
            return;
        }
    }
};

template<typename _Async_TCPSocket /*= Async_TCPSocket*/>
struct Sender_Accept : Sender_LogSimple<_Async_TCPSocket, std::error_code>
{
    SOCKET _listen_socket = INVALID_SOCKET;
    wi::IoCompletionPort* _iocp = nullptr;

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_Accept<Receiver_, _Async_TCPSocket>{{}, std::move(receiver), _listen_socket, _iocp};
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
        return Sender_Connect{{}, _socket, endpoint};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_socket_acceptor/async_accept/overload3.html.
    Sender_Accept<Async_TCPSocket> async_accept() noexcept
    {
        return Sender_Accept<Async_TCPSocket>{{}, _socket, _iocp};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_stream_socket/async_write_some.html.
    // See also async_write().
    Sender_WriteSome async_write_some(std::span<char> data) noexcept
    {
        return Sender_WriteSome{{}, _socket, data};
    }

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/reference/basic_stream_socket/async_read_some.html.
    // See also async_read().
    Sender_ReadSome async_read_some(std::span<char> buffer) noexcept
    {
        return Sender_ReadSome{{}, _socket, buffer};
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
struct Operation_IOCP_Schedule : Operation_Base
{
    static constexpr wi::WinULONG_PTR kScheduleKeyIOCP = 2;

    Receiver _receiver;
    wi::IoCompletionPort* _iocp = nullptr;
    IOCP_Overlapped _ov{{}, &Operation_IOCP_Schedule::on_scheduled, this};

    static void on_scheduled(void* user_data, const wi::PortEntry& entry, std::error_code ec) noexcept
    {
        assert(user_data);
        Operation_IOCP_Schedule& self = *static_cast<Operation_IOCP_Schedule*>(user_data);
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

    void start() & noexcept
    {
        wi::PortEntry entry{};
        entry.bytes_transferred = 0;
        entry.completion_key = kScheduleKeyIOCP;
        entry.overlapped = _ov.ptr();
        std::error_code ec;
        _iocp->post(entry, ec);
        if (ec)
        {
            on_scheduled(this, entry, ec);
            return;
        }
    }
};

struct Sender_IOCP_Schedule : Sender_LogSimple<void, std::error_code>
{
    wi::IoCompletionPort* _iocp;

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_IOCP_Schedule<Receiver_>{{}, std::move(receiver), _iocp};
    }
};

static Sender_IOCP_Schedule IOCP_schedule(wi::IoCompletionPort& iocp)
{
    return Sender_IOCP_Schedule{{}, &iocp};
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
struct Operation_SelectOnceInN : Operation_Base
{
    struct Receiver_OnScheduled
    {
        Operation_SelectOnceInN* _self = nullptr;

        void set_value() && noexcept
        {
            _self->destroy_state();
            unifex::set_value(std::move(_self->_receiver));
        }
        template<typename E>
        void set_error(E&& arg) && noexcept
        {
            _self->destroy_state();
            unifex::set_error(std::move(_self->_receiver), std::forward<E>(arg));
        }
        void set_done() && noexcept
        {
            _self->destroy_state();
            unifex::set_done(std::move(_self->_receiver));
        }
    };

    using Op_Scheduler = decltype(unifex::connect(
        std::declval<Scheduler&>().schedule()
        , std::declval<Receiver_OnScheduled>()));
    using Op_Fallback = decltype(unifex::connect(
        std::declval<Fallback&>().schedule()
        , std::declval<Receiver_OnScheduled>()));

    using Op_State = std::variant<std::monostate
        , unifex::manual_lifetime<Op_Scheduler>
        , unifex::manual_lifetime<Op_Fallback>>;

    Receiver _receiver;
    unsigned* _counter = nullptr;
    unsigned _N = 0;
    Scheduler _scheduler; // no_unique_address
    Fallback _fallback; // no_unique_address
    Op_State _op{};

    void destroy_state()
    {
        if (auto* scheduler = std::get_if<1>(&_op))
        {
            scheduler->destruct();
        }
        else if (auto* fallback = std::get_if<2>(&_op))
        {
            fallback->destruct();
        }
        else
        {
            assert(false);
        }
    }

    void start() & noexcept
    {
        assert(_counter);
        assert(_N > 0);
        const unsigned counter = ++(*_counter);
        if ((counter % _N) == 0)
        {
            // Select & run Scheduler.
            auto& storage = _op.template emplace<1>();
            storage.construct_with([&]()
            {
                return unifex::connect(
                    _scheduler.schedule()
                    , Receiver_OnScheduled{this});
            });
            unifex::start(storage.get());
        }
        else
        {
            // Select & run Fallback Scheduler.
            auto& storage = _op.template emplace<2>();
            storage.construct_with([&]()
            {
                return unifex::connect(
                    _fallback.schedule()
                    , Receiver_OnScheduled{this});
            });
            unifex::start(storage.get());
        }
    }
};

// #XXX: error should be variant<> from both schedulers.
template<typename Scheduler, typename Fallback>
struct Sender_SelectOnceInN : Sender_LogSimple<void, void>
{
    unsigned* _counter = nullptr;
    unsigned _N = 0;
    Scheduler _scheduler; // no_unique_address
    Fallback _fallback; // no_unique_address

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_SelectOnceInN<Receiver_, Scheduler, Fallback>{{}
            , std::move(receiver), _counter, _N, _scheduler, _fallback};
    }
};

template<typename Scheduler, typename Fallback = unifex::inline_scheduler>
struct SelectOnceInN_Scheduler
{
    Scheduler _scheduler; // no_unique_address
    unsigned* _counter;
    unsigned _N = 128;
    Fallback _fallback{}; // no_unique_address

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
        return Sender_SelectOnceInN<Scheduler, Fallback>{{}, _counter, _N, _scheduler, _fallback};
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
        unsigned _calls_count = 0;

        auto scheduler()
        {
            // Schedule once in 128 calls into IOCP, otherwise inline_sheduler.
            return SelectOnceInN_Scheduler<IOCP_Scheduler>{
                IOCP_Scheduler{_socket._iocp}, &_calls_count, 128};
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
        unsigned _calls_count = 0;

        auto scheduler()
        {
            return SelectOnceInN_Scheduler<IOCP_Scheduler>{
                IOCP_Scheduler{_socket._iocp}, &_calls_count, 128};
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

struct EndpointsList_IPv4
{
    PADDRINFOEXW _address_list = nullptr;
    explicit EndpointsList_IPv4(PADDRINFOEXW list) noexcept
        : _address_list{list} {}
    ~EndpointsList_IPv4() { destroy(); }
    EndpointsList_IPv4(const EndpointsList_IPv4&) = delete;
    EndpointsList_IPv4& operator=(const EndpointsList_IPv4&) = delete;
    EndpointsList_IPv4(EndpointsList_IPv4&& rhs) noexcept
        : _address_list(std::exchange(rhs._address_list, nullptr)) { }
    EndpointsList_IPv4& operator=(EndpointsList_IPv4&& rhs) noexcept
    {
        if (this != &rhs)
        {
            destroy();
            _address_list = std::exchange(rhs._address_list, nullptr);
        }
        return *this;
    }

    struct iterator
    {
        PADDRINFOEXW _address_list = nullptr;

        template<class Reference>
        struct arrow_proxy
        {
            Reference _ref;
            Reference* operator->()
            {
                return &_ref;
            }
        };

        using iterator_category = std::forward_iterator_tag;
        using value_type = const Endpoint_IPv4;
        using reference = const Endpoint_IPv4;
        using pointer = arrow_proxy<const Endpoint_IPv4>;
        using difference_type = std::ptrdiff_t;

        reference operator*() const { return get(); }
        pointer operator->() const { return pointer{get()}; }

        bool operator==(const iterator& rhs) const { return (_address_list == rhs._address_list); }
        bool operator!=(const iterator& rhs) const { return (_address_list != rhs._address_list); }

        Endpoint_IPv4 get() const
        {
            assert(_address_list);
            assert(_address_list->ai_addrlen == sizeof(sockaddr_in));
            const struct sockaddr_in* ipv4 =
                reinterpret_cast<const struct sockaddr_in*>(_address_list->ai_addr);
            Endpoint_IPv4 endpoint;
            endpoint._ip_network = ipv4->sin_addr.s_addr;
            endpoint._port_network = ipv4->sin_port;
            return endpoint;
        }

        iterator operator++()
        {
            assert(_address_list);
            _address_list = _address_list->ai_next;
            return iterator{_address_list};
        }
        iterator operator++(int)
        {
            assert(_address_list);
            PADDRINFOEXW current = _address_list;
            _address_list = _address_list->ai_next;
            return iterator{current};
        }
    };

    iterator begin() { return iterator{_address_list}; }
    iterator begin() const { return iterator{_address_list}; }
    iterator end() { return iterator{}; }
    iterator end() const { return iterator{}; }

private:
    void destroy()
    {
        PADDRINFOEXW ptr = std::exchange(_address_list, nullptr);
        if (ptr)
        {
            ::FreeAddrInfoExW(ptr);
        }
    }
};

template<typename Receiver
    , typename StopToken = unifex::stop_token_type_t<Receiver>>
struct Operation_Resolve : Operation_Base
{
    struct AddrInfo_Overlapped : WSAOVERLAPPED
    {
        using Callback = void (Operation_Resolve::*)(DWORD /*error*/, DWORD /*bytes*/);
        Callback _callback = nullptr;
        Operation_Resolve* _self = nullptr;
    };

    struct Callback_Stop
    {
        Operation_Resolve* _self = nullptr;
        void operator()() noexcept { _self->try_stop(); }
    };
    using StopCallback = typename StopToken::template callback_type<Callback_Stop>;

    Receiver _receiver;
    wi::IoCompletionPort* _iocp = nullptr;
    std::string_view _host_name;
    std::string_view _service_name_or_port;
    // Output.
    wchar_t _whost_name[256]{};
    wchar_t _wservice_name[256]{};
    PADDRINFOEXW _results = nullptr;
    AddrInfo_Overlapped _ov{{}, &Operation_Resolve::on_complete_WindowsThread, this};
    IOCP_Overlapped _iocp_ov{{}, &Operation_Resolve::on_addrinfo_finish, this};
    unifex::manual_lifetime<StopCallback> _stop_callback{};
    HANDLE _cancel_handle = INVALID_HANDLE_VALUE;
    // Points to `_cancel_handle` when cancel is available.
    std::atomic<HANDLE*> _atomic_stop{};

    static void on_addrinfo_finish(void* user_data, const wi::PortEntry& entry, std::error_code ec)
    {
        Operation_Resolve* self = static_cast<Operation_Resolve*>(user_data);

        if constexpr (!unifex::is_stop_never_possible_v<StopToken>)
        {
            // #XXX: There is race if someone asks to stop now.
            self->_cancel_handle = nullptr; // Invalid now.
            self->_atomic_stop = nullptr;
            self->_stop_callback.destruct();

            auto stop_token = unifex::get_stop_token(self->_receiver);
            if (stop_token.stop_requested())
            {
                // set_done() even if we happened to succeed.
                unifex::set_done(std::move(self->_receiver));
                return;
            }
        }

        if (ec)
        {
            unifex::set_error(std::move(self->_receiver), ec);
            return;
        }
        // error, see on_complete_WindowsThread().
        const DWORD error = DWORD(entry.completion_key);
        if (error != 0)
        {
            unifex::set_error(std::move(self->_receiver)
                , std::error_code(int(error), std::system_category()));
            return;
        }
        unifex::set_value(std::move(self->_receiver)
            , EndpointsList_IPv4{self->_results});
    }

    void on_complete_WindowsThread(DWORD error, DWORD bytes)
    {
        // #XXX: need to invalidate _cancel_handle/_atomic_stop there.
        wi::PortEntry entry{};
        entry.overlapped = _iocp_ov.ptr();
        entry.completion_key = error;
        entry.bytes_transferred = bytes;
        std::error_code ec;
        _iocp->post(entry, ec);
        if (ec) // Bad. Invoking user code on external ws2_32.dll, TppWorkerThread.
        {
            on_addrinfo_finish(this, entry, ec);
        }
    }

    static void CALLBACK OnAddrInfoEx_CompleteCallback(DWORD dwError, DWORD dwBytes, LPWSAOVERLAPPED lpOverlapped)
    {
        AddrInfo_Overlapped* ov = static_cast<AddrInfo_Overlapped*>(lpOverlapped);
        Operation_Resolve& self = *ov->_self;
        auto callback = ov->_callback;
        (self.*callback)(dwError, dwBytes);
    }

    void try_stop() noexcept
    {
        HANDLE* cancel_handle = _atomic_stop.exchange(nullptr);
        if (!cancel_handle)
        {
            // Operation is not yet started or is in finish callback.
            return;
        }

        const INT error = ::GetAddrInfoExCancel(cancel_handle);
        (void)error; // Not much can be done.
    }

    void start() & noexcept
    {
        // See also "Internationalized Domain Names":
        // https://docs.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexw#internationalized-domain-names.
        if (_host_name.size() > 0)
        {
            const int error = ::MultiByteToWideChar(CP_ACP
                , MB_PRECOMPOSED
                , _host_name.data()
                , int(_host_name.size())
                , _whost_name
                , int(std::size(_whost_name)));
            if (error == 0)
            {
                assert(false);
                return;
            }
        }
        if (_service_name_or_port.size() > 0)
        {
            const int error = ::MultiByteToWideChar(CP_ACP
                , MB_PRECOMPOSED
                , _service_name_or_port.data()
                , int(_service_name_or_port.size())
                , _wservice_name
                , int(std::size(_wservice_name)));
            if (error == 0)
            {
                assert(false);
                return;
            }
        }

        // Don't even alloc (?) cancel handle if not stoppable.
        auto stop_token = unifex::get_stop_token(_receiver);
        HANDLE* cancel_ptr = (stop_token.stop_possible() ? &_cancel_handle : nullptr);

        if constexpr (!unifex::is_stop_never_possible_v<StopToken>)
        {
            _stop_callback.construct(stop_token, Callback_Stop{this});
        }

        ADDRINFOEXW hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        const INT error = ::GetAddrInfoExW(_whost_name, _wservice_name
            , NS_ALL
            , nullptr // lpNspId
            , &hints
            , &_results
            , nullptr // timeout
            , &_ov
            , &OnAddrInfoEx_CompleteCallback
            , cancel_ptr);
        if (error == 0)
        {
            wi::PortEntry entry{};
            entry.overlapped = _iocp_ov.ptr();
            entry.completion_key = 0;
            entry.bytes_transferred = 0;
            // Handles concurrent stop request if any.
            on_addrinfo_finish(this, entry, std::error_code());
            return;
        }
        if (error != WSA_IO_PENDING)
        {
            wi::PortEntry entry{};
            entry.overlapped = _iocp_ov.ptr();
            entry.completion_key = error;
            entry.bytes_transferred = 0;
            // Handles concurrent stop request if any.
            on_addrinfo_finish(this, entry, std::error_code());
            return;
        }

        if constexpr (!unifex::is_stop_never_possible_v<StopToken>)
        {
            // #XXX: there is race between doing a call to ::GetAddrInfoExW()
            // - creating `_cancel_handle` - and stop request from
            // stop source. In this case we have to way to actually interrupt
            // ::GetAddrInfoExW() and will simply fail to stop.
            if (cancel_ptr)
            {
                _atomic_stop = cancel_ptr;
            }
        }
    }
};

struct Sender_Resolve : Sender_LogSimple<EndpointsList_IPv4, std::error_code>
{
    wi::IoCompletionPort* _iocp;
    std::string_view _host_name;
    std::string_view _service_name_or_port;

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_Resolve<Receiver_>{{}
            , std::move(receiver), _iocp, _host_name, _service_name_or_port};
    }
};

// IPv4.
static Sender_Resolve async_resolve(wi::IoCompletionPort& iocp
    , std::string_view host_name
    // Either service name, like "http", see "%WINDIR%\system32\drivers\etc\services" on Windows
    // OR port, like "80".
    , std::string_view service_name_or_port = {})
{
    return Sender_Resolve{{}, &iocp, host_name, service_name_or_port};
}

template<typename Receiver, typename EndpointsRange>
struct Operation_RangeConnect : Operation_Base
{
    struct Receiver_Connect
    {
        Operation_RangeConnect* _self;
        void set_value() && noexcept { _self->on_connect_success(); }
        void set_error(std::error_code ec) && noexcept { _self->on_connect_error(ec); }
        void set_error(std::exception_ptr) && noexcept { _self->on_connect_error(std::error_code(-1, std::system_category())); }
        void set_done() && noexcept { assert(false); }
    };

    using Op = Operation_Connect<Receiver_Connect>;
    using Iterator = typename EndpointsRange::iterator;

    Receiver _receiver;
    Async_TCPSocket* _socket = nullptr;
    EndpointsRange _endpoints;
    // Output.
    Iterator _current_endpoint{};
    std::error_code _last_error{};
    unifex::manual_lifetime<Op> _op{};

    void on_connect_success() noexcept
    {
        destroy_storage();
        unifex::set_value(std::move(_receiver), *_current_endpoint);
    }

    void on_connect_error(std::error_code ec) noexcept
    {
        destroy_storage();
        _last_error = ec;
        ++_current_endpoint;
        start_next_connect();
    }

    void destroy_storage() noexcept
    {
        _op.destruct();
    }

    void start_next_connect()
    {
        if (_current_endpoint == std::end(_endpoints))
        {
            unifex::set_error(std::move(_receiver), _last_error);
            return;
        }

        _op.construct_with([&]()
        {
            return unifex::connect(
                _socket->async_connect(*_current_endpoint)
                , Receiver_Connect{this});
        });
        unifex::start(_op.get());
    }

    void start() & noexcept
    {
        _last_error = std::error_code(-1, std::system_category()); // Empty.
        _current_endpoint = std::begin(_endpoints);
        start_next_connect();
    }
};

template<typename EndpointsRange>
struct Sender_RangeConnect : Sender_LogSimple<Endpoint_IPv4, std::error_code>
{
    Async_TCPSocket* _socket;
    EndpointsRange _endpoints;

    template<typename Receiver>
    auto connect(Receiver&& receiver) && noexcept
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return Operation_RangeConnect<Receiver_, EndpointsRange>{{}
            , std::move(receiver), _socket, std::move(_endpoints)};
    }
};

template<typename EndpointsRange>
static auto async_connect(Async_TCPSocket& socket, EndpointsRange&& endpoints)
{
    // #XXX: implement when_first() algorithm.
    using EndpointsRange_ = std::remove_cvref_t<EndpointsRange>;
    return Sender_RangeConnect<EndpointsRange_>{{}, &socket
        , std::forward<EndpointsRange>(endpoints)};
}

struct StopReceiver
{
    bool& _finish;

    // Catch-all.
    friend void tag_invoke(auto, StopReceiver&& self, auto&&...) noexcept
    {
        self._finish = true;
    }
};

template<typename Sender>
static void IOCP_sync_wait(wi::IoCompletionPort& iocp, Sender&& sender)
{
    bool finish = false;
    auto op_state = unifex::connect(std::forward<Sender>(sender), StopReceiver{finish});
    unifex::start(op_state);
    std::error_code ec;
    while (!finish)
    {
        wi::PortEntry entries[4];
        for (const wi::PortEntry& entry : iocp.get_many(entries, ec))
        {
            IOCP_Overlapped* ov = static_cast<IOCP_Overlapped*>(entry.overlapped);
            assert(ov);
            assert(ov->_callback);
            ov->_callback(ov->_user_data, entry, ec);
        }
    }
}

template<typename StopSource, typename Sender>
static auto stop_with(StopSource& stop_source, Sender&& sender)
{
    return unifex::with_query_value(
        std::forward<Sender>(sender)
        , unifex::get_stop_token
        , stop_source.get_token());
}

#include <vector>
#include <chrono>

#include <cstring>

static void main_client_server()
{
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
    int error = ::setsockopt(server_socket->_socket, SOL_SOCKET, SO_REUSEADDR
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
                | unifex::then([]()
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

    IOCP_sync_wait(*iocp, unifex::when_all(server_logic(), client_logic()));

    assert(memcmp(write_data.data(), read_data.data(), write_data.size()) == 0);
    assert(memcmp(write_data.data(), server_data.data(), write_data.size()) == 0);
}

static void main_resolve()
{
    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    auto client = Async_TCPSocket::make(*iocp, ec);
    assert(!ec);

    unifex::inplace_stop_source stop_source;

    auto test_logic = [&]()
    {
        return unifex::let_value(stop_with(stop_source, async_resolve(*iocp, "www.google.com", "80"))
            , [&](EndpointsList_IPv4& endpoints)
        {
            return async_connect(*client, std::move(endpoints));
        })
            | unifex::then([](Endpoint_IPv4)
        {
            printf("Connected!\n");
        });
    };

    IOCP_sync_wait(*iocp, test_logic());
}

int main(int argc, char**)
{
    Initialize_WSA wsa;
    const auto start = std::chrono::steady_clock::now();

    if (argc >= 1)
        main_client_server();
    else if (argc >= 2)
        main_resolve();

    const auto end = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration_cast<std::chrono::duration<float>>(end - start).count();
    printf("Elapsed: %.3f seconds.\n", seconds);
}
