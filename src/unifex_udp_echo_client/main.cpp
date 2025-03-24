#include "unifex_IOCP_sockets.hpp"
#include <cstdio>

template<typename Sender>
static auto unroll(Sender&& sender, wi::IoCompletionPort& iocp, unsigned& calls_count)
{
    auto scheduler = SelectOnceInN_Scheduler<IOCP_Scheduler>{
        IOCP_Scheduler{&iocp}, &calls_count, 32};
    return unifex::on(std::move(scheduler), std::forward<Sender>(sender));
}

struct Client
{
    Async_UDPSocket* _socket = nullptr;
    std::span<char> _buffer{};
    const Endpoint_IPv4 _receiver{};
    Endpoint_IPv4 _sender{};
    unsigned _calls_count = 0;
};

static auto write_read(Client& state)
{
    return unifex::defer([&]() mutable
    {
        return unifex::repeat_effect(unifex::defer([&]()
        {
            return unifex::let_value(state._socket->async_send_to(state._buffer, state._receiver)
                , [&](std::size_t& n)
            {
                printf("write='%.*s'.\n", int(n), state._buffer.data());
                return unroll(state._socket->async_receive_from(state._buffer, state._sender)
                        , *state._socket->_iocp, state._calls_count)
                    | unifex::then([&state](std::size_t n)
                    {
                        printf("read='%.*s'.\n", int(n), state._buffer.data());
                    });
            });
        }));
    });
}

int main()
{
    Initialize_WSA wsa;
    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    std::optional<Async_UDPSocket> client = Async_UDPSocket::make(*iocp, ec);
    assert(!ec);
    assert(client);

    char data[] = "udp-data-to-send";
    const auto receiver = Endpoint_IPv4::from_string("127.0.0.1", 60261);
    assert(receiver);

    Client state{&(*client), data, *receiver};
    IOCP_sync_wait(*iocp, write_read(state));
}
