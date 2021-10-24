// Derived from https://github.com/dietmarkuehl/kuhllib/blob/32cfa616d0820f64e4c69fc8dd60a396656b42ac/src/examples/echo_server.cpp.
// See also Asio: https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/echo/async_tcp_echo_server.cpp.
// 
#include "unifex_IOCP_sockets.hpp"
#include <unifex/let_error.hpp>
#include <cstdio>

struct Connection
{
    Async_TCPSocket _socket;
    char _buffer[1024]{};
};

static auto read_some_write(Connection& connection)
{
    return unifex::repeat_effect(unifex::defer([&connection]()
    {
        return unifex::let_value(connection._socket.async_read_some(connection._buffer)
            , [&connection](std::size_t& n)
        {
            printf("read='%.*s'.\n", int(n), connection._buffer);
            return async_write(connection._socket, std::span(connection._buffer, n))
                // Ignore return value.
                | unifex::then([](auto&&...) {});
        });
    }));
}

static void run_client(Async_TCPSocket socket)
{
    // Alloc & start new connection, detached.
    start_detached(unifex::defer(
        [connection = Connection{std::move(socket)}]() mutable
    {
        printf("Client connected.\n");
        return read_some_write(connection);
    }) 
        | unifex::let_error([]()
    {
        printf("Client error.\n");
        return unifex::just();
    }));
}

static auto run_server(Async_TCPSocket& server)
{
    return unifex::repeat_effect(unifex::defer([&server]()
    {
        return server.async_accept();
    })
        | unifex::then([](Async_TCPSocket socket)
    {
        run_client(std::move(socket));
    }));
}

int main()
{
    Initialize_WSA wsa;
    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    std::optional<Async_TCPSocket> server = Async_TCPSocket::make(*iocp, ec);
    assert(!ec);
    assert(server);

    server->bind_and_listen(Endpoint_IPv4::any(60260), ec);
    assert(!ec);

    IOCP_sync_wait(*iocp, run_server(*server));
}
