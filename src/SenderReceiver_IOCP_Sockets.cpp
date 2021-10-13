#include <winsock2.h>
#include <ws2tcpip.h>

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <Mswsock.h>

#pragma comment(lib, "Ws2_32.lib")

#include "io_completion_port.h"

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

#include <unifex/sender_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/sequence.hpp>

#define XX_ENABLE_STATE_LOGS() 0
#define XX_ENABLE_SENDERS_LOGS() 0

static constexpr wi::WinULONG_PTR kClientKeyIOCP = 1;

struct IOCP_Overlapped : OVERLAPPED
{
    using Handle = void (*)(void* user_data, const wi::PortEntry& /*entry*/, std::error_code /*ec*/);
    Handle _callback = nullptr;
    void* _user_data = nullptr;
};

struct State_Log
{
    State_Log(const State_Log&) = delete;
    State_Log& operator=(const State_Log&) = delete;
    State_Log(State_Log&&) = delete;
    State_Log& operator=(State_Log&&) = delete;
protected:
    const char* _name = nullptr;

    State_Log(const char* name)
        : _name(name)
    {
#if (XX_ENABLE_STATE_LOGS())
        printf("[State] '%s' c-tor.\n", _name);
#endif
    }
    ~State_Log()
    {
#if (XX_ENABLE_STATE_LOGS())
        printf("[State] '%s' d-tor.\n", _name);
#endif
    }

    void log(const char* debug)
    {
#if (XX_ENABLE_STATE_LOGS())
        printf("[State] '%s' - %s.\n", _name, debug);
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
    ~Sender_LogSimple()
    {
#if (XX_ENABLE_SENDERS_LOGS())
        printf("[Sender] '%s' d-tor.\n", _name);
#endif
    }
    void log(const char* debug)
    {
#if (XX_ENABLE_SENDERS_LOGS())
        printf("[State] '%s' - %s.\n", _name, debug);
#endif
    }
};

template<typename Receiver>
struct State_Connect : State_Log
{
    explicit State_Connect(Receiver&& receiver, SOCKET socket, const char* ip_v4_str, unsigned short port)
        : State_Log("connect")
        , _receiver(std::move(receiver))
        , _ov{{}, &State_Connect::on_connected, this}
        , _socket(socket)
        , _ip_v4_str(ip_v4_str)
        , _port(port) { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    const char* _ip_v4_str = nullptr;
    unsigned short _port = 0;

    static void on_connected(void* user_data, const wi::PortEntry& entry, std::error_code ec)
    {
        assert(user_data);
        State_Connect& self = *static_cast<State_Connect*>(user_data);
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

        unifex::set_value(std::move(self._receiver), -1/*unused*/);
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, State_Connect& self) noexcept
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
        connect_to.sin_port = htons(self._port);
        const int ok = inet_pton(AF_INET, self._ip_v4_str, &connect_to.sin_addr);
        assert(ok == 1);

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

struct Sender_Connect : Sender_LogSimple<int, std::error_code>
{
    explicit Sender_Connect(SOCKET socket, const char* ip_v4_str, unsigned short port) noexcept
        : Sender_LogSimple<int, std::error_code>("connect")
        , _socket(socket)
        , _ip_v4_str(ip_v4_str)
        , _port(port) { }

    SOCKET _socket = INVALID_SOCKET;
    const char* _ip_v4_str = nullptr;
    unsigned short _port = 0;

    template <typename Receiver>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , Sender_Connect&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return State_Connect<Reveiver_>(std::move(receiver)
            , self._socket, self._ip_v4_str, self._port);
    }
};

template<typename Receiver>
struct State_SendSome : State_Log
{
    explicit State_SendSome(Receiver&& receiver, SOCKET socket, std::span<char> data)
        : State_Log("send_some")
        , _receiver(std::move(receiver))
        , _ov{{}, &State_SendSome::on_sent, this }
        , _socket(socket)
        , _data(data) { }

    Receiver _receiver;
    IOCP_Overlapped _ov;
    SOCKET _socket = INVALID_SOCKET;
    std::span<char> _data;

    static void on_sent(void* user_data, const wi::PortEntry& entry, std::error_code ec)
    {
        assert(user_data);
        State_SendSome& self = *static_cast<State_SendSome*>(user_data);
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
            , self._data.first(entry.bytes_transferred));
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, State_SendSome& self) noexcept
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

struct Sender_SendSome : Sender_LogSimple<std::span<char>, std::error_code>
{
    explicit Sender_SendSome(SOCKET socket, std::span<char> data) noexcept
        : Sender_LogSimple<std::span<char>, std::error_code>("send_some")
        , _socket(socket)
        , _data(data) { }

    SOCKET _socket;
    std::span<char> _data;

    template <typename Receiver>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , Sender_SendSome&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return State_SendSome<Reveiver_>(std::move(receiver), self._socket, self._data);
    }
};

template<typename Receiver>
struct State_ReceiveSome : State_Log
{
    explicit State_ReceiveSome(Receiver&& receiver, SOCKET socket, std::span<char> buffer)
        : State_Log("receive_some")
        , _receiver(std::move(receiver))
        , _ov{{}, &State_ReceiveSome::on_received, this}
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
        State_ReceiveSome& self = *static_cast<State_ReceiveSome*>(user_data);
        self.log("on_received");

        assert(entry.bytes_transferred > 0);
        assert(entry.completion_key == kClientKeyIOCP);
        assert(entry.overlapped == &self._ov);

        if (ec)
        {
            unifex::set_error(std::move(self._receiver), ec);
            return;
        }

        unifex::set_value(std::move(self._receiver)
            , self._buffer.first(entry.bytes_transferred));
    }

    friend void tag_invoke(unifex::tag_t<unifex::start>, State_ReceiveSome& self) noexcept
    {
        self.log("start");

        assert(self._buffer.size() > 0);

        WSABUF receive_buffer{};
        receive_buffer.buf = self._buffer.data();
        receive_buffer.len = ULONG(self._buffer.size());
        self._flags = MSG_PARTIAL;
        const int error = ::WSARecv(self._socket
            , &receive_buffer, 1
            , nullptr
            , &self._flags
            , &self._ov
            , nullptr);
        if (error == 0)
        {
            // Completed synchronously.
            wi::PortEntry entry;
            entry.bytes_transferred = wi::WinDWORD(self._buffer.size());
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

struct Sender_ReceiveSome : Sender_LogSimple<std::span<char>, std::error_code>
{
    explicit Sender_ReceiveSome(SOCKET socket, std::span<char> buffer) noexcept
        : Sender_LogSimple<std::span<char>, std::error_code>("receive_some")
        , _socket(socket)
        , _buffer(buffer) { }

    SOCKET _socket;
    std::span<char> _buffer;

    template <typename Receiver>
    friend auto tag_invoke(unifex::tag_t<unifex::connect>
        , Sender_ReceiveSome&& self, Receiver&& receiver) noexcept
    {
        self.log("connect");
        using Reveiver_ = std::remove_cvref_t<Receiver>;
        return State_ReceiveSome<Reveiver_>(std::move(receiver), self._socket, self._buffer);
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

    Sender_Connect async_connect(const char* ip_v4_str, unsigned short port)
    {
        return Sender_Connect{_socket, ip_v4_str, port};
    }

    Sender_SendSome async_send_some(std::span<char> data)
    {
        return Sender_SendSome{_socket, data};
    }

    Sender_ReceiveSome async_receive_some(std::span<char> buffer)
    {
        return Sender_ReceiveSome{_socket, buffer};
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

    char send_data[] = "Test";
    char receive_data[1024]{};

    auto logic = [&]()
    {
        return unifex::sequence(
              socket->async_connect("127.0.0.1", 60260)
                | unifex::then([](int)
                {
                    printf("Connected!.\n");
                })
            , socket->async_send_some(send_data)
                | unifex::then([](std::span<char> data)
                {
                    printf("Sent %i bytes!.\n", int(data.size()));
                })
            , socket->async_receive_some(receive_data)
                | unifex::then([](std::span<char> buffer)
                {
                    printf("Received %i bytes: %.*s!.\n"
                        , int(buffer.size()), int(buffer.size()), buffer.data());
                })
            );
    };

    bool finish = false;
    auto state = unifex::connect(logic(), StopReceiver{finish});
    unifex::start(state);

    while (!finish)
    {
        wi::PortEntry entries[4];
        std::error_code ec;
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
