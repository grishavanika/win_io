#include "unifex_IOCP_sockets.hpp"
#include <cstdio>

template<typename Sender>
static auto unroll(Sender&& sender, wi::IoCompletionPort& iocp, unsigned& calls_count)
{
    auto scheduler = SelectOnceInN_Scheduler<IOCP_Scheduler>{
        IOCP_Scheduler{&iocp}, &calls_count, 32};
    return unifex::on(std::move(scheduler), std::forward<Sender>(sender));
}

static auto write_read(Async_TCPSocket& socket, std::span<char> buffer)
{
    return unifex::defer([&, buffer, calls_count = unsigned(0)]() mutable
    {
        return unifex::repeat_effect(unifex::defer([&]()
        {
            return unifex::let_value(async_write(socket, buffer)
                , [&](std::size_t& n)
            {
                printf("write='%.*s'.\n", int(n), buffer.data());
                return unroll(async_read(socket, buffer), *socket._iocp, calls_count)
                    // Ignore return value.
                    | unifex::then([](auto&&...) {});
            });
        }));
    });
}

static auto run_client(Async_TCPSocket& socket
    , std::span<char> buffer
    , std::string_view port
    , std::string_view host_name = "localhost")
{
    return unifex::let_value(async_resolve(*socket._iocp, host_name, port)
        , [&, buffer](EndpointsList_IPv4& endpoints)
    {
        return unifex::let_value(async_connect(socket, std::move(endpoints))
            , [&, buffer](Endpoint_IPv4)
        {
            return write_read(socket, buffer);
        });
    });
}

int main()
{
    Initialize_WSA wsa;
    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    std::optional<Async_TCPSocket> client = Async_TCPSocket::make(*iocp, ec);
    assert(!ec);
    assert(client);

    char data[] = "data-to-send";
    IOCP_sync_wait(*iocp, run_client(*client, data, "60260"));
}
