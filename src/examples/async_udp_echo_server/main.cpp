#include "unifex_IOCP_sockets.hpp"
#include <cstdio>

struct Server
{
    Async_UDPSocket* _server = nullptr;
    std::span<char> _buffer{};
    Endpoint_IPv4 _sender{};
};

static auto read_some_write(Server& state)
{
    return unifex::repeat_effect(unifex::defer([&state]()
    {
        return unifex::let_value(state._server->async_receive_from(state._buffer, state._sender)
            , [&state](std::size_t& n)
        {
            printf("read='%.*s'.\n", int(n), state._buffer.data());
            return state._server->async_send_to(state._buffer.first(n), state._sender)
                | unifex::then([&state](std::size_t n)
            {
                printf("write='%.*s'.\n", int(n), state._buffer.data());
            });
        });
    }));
}

// See http://www.serverframework.com/products---the-free-framework.html.
// 

int main()
{
    Initialize_WSA wsa;
    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    std::optional<Async_UDPSocket> server = Async_UDPSocket::make(*iocp, ec);
    assert(!ec);
    assert(server);

    server->bind(Endpoint_IPv4::any(60261), ec);
    assert(!ec);

    char buffer[1024]{};
    Server state{&(*server), buffer};
    IOCP_sync_wait(*iocp, read_some_write(state));
}
