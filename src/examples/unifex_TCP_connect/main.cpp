#include "unifex_IOCP_sockets.hpp"

#include <unifex/let_done.hpp>
#include <unifex/let_error.hpp>

#include <chrono>

int main()
{
    Initialize_WSA wsa;
    std::error_code ec;
    auto iocp = wi::IoCompletionPort::make(ec);
    assert(!ec);
    assert(iocp);

    auto client = Async_TCPSocket::make(*iocp, ec);
    assert(!ec);

    unifex::inplace_stop_source stop_source;

    auto test_logic = [&]()
    {
        return unifex::let_value(stop_with(stop_source, async_resolve(*iocp, Resolve_Hint::TCP(), "www.google.com", "80"))
            , [&](EndpointsList_IPv4& endpoints)
        {
            return async_connect(*client, std::move(endpoints));
        })
            | unifex::then([](Endpoint_IPv4)
        {
            printf("Connected!\n");
        })
            | unifex::let_done([]()
        {
            printf("Cancelled.\n");
            return unifex::just();
        })
            | unifex::let_error([]()
        {
            printf("Error.\n");
            return unifex::just();
        });
    };

    const auto start = std::chrono::steady_clock::now();

    IOCP_sync_wait(*iocp, test_logic());

    const auto end = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration_cast<std::chrono::duration<float>>(end - start).count();
    printf("Elapsed: %.3f seconds.\n", seconds);
}
