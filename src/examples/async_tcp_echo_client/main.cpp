#include "unifex_IOCP_sockets.hpp"
#include <cstdio>

static auto unroll(wi::IoCompletionPort& iocp, unsigned& calls_count)
{
    return SelectOnceInN_Scheduler<IOCP_Scheduler>{
        IOCP_Scheduler{&iocp}, &calls_count, 16};
}

static auto write_some_read(Async_TCPSocket& socket, std::span<char> buffer)
{
    return unifex::let_value(unifex::just(unsigned(0))
        , [&, buffer](unsigned& calls_count)
    {
        return unifex::repeat_effect(unifex::defer([&, buffer]()
        {
            return unifex::let_value(async_write(socket, buffer)
                , [&, buffer](std::size_t& n)
            {
                printf("write='%.*s'.\n", int(n), buffer.data());
                return unifex::on(unroll(*socket._iocp, calls_count)
                    , async_read(socket, buffer)
                        // Ignore return value.
                        | unifex::then([](auto&&...) {}));
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
            return write_some_read(socket, buffer);
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
