#include "unifex_IOCP_sockets.hpp"

#include <chrono>

#include <cstring>

int main()
{
    // The data client sends to the server.
    std::vector<char> write_data;
    write_data.resize(1ull * 1024 * 1024 * 1024 * 1, 'x');
    // The data client reads the response to.
    std::vector<char> read_data;
    read_data.resize(write_data.size());

    // The data server reads to and writes to.
    std::vector<char> server_data;
    server_data.resize(write_data.size());

    // Scattered buffers client sends.
    std::vector<BufferRef> to_write;

    const std::size_t buffer_size_KBs = 1024;
    std::size_t processed = 0;
    while (processed < write_data.size())
    {
        const std::size_t remaining = (write_data.size() - processed);
        const std::size_t size = (std::min)(remaining, std::size_t(1 * 1024 * buffer_size_KBs));
        to_write.push_back(BufferRef(&write_data[processed], std::uint32_t(size)));
        processed += size;
    }

    ///////////////////////////////////////////////////////////////////////////
    // 
    Initialize_WSA wsa;
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

    server_socket->bind_and_listen(Endpoint_IPv4::any(60260), ec);
    assert(!ec);

    auto server_logic = [&]()
    {
        return unifex::let_value(unifex::just(Async_TCPSocket())
            , [&](Async_TCPSocket& client)
        {
            return unifex::sequence(
                server_socket->async_accept()
                    | unifex::then([&client](Async_TCPSocket accepted_client)
                {
                    client = std::move(accepted_client);
                    printf("[Server] Accepted.\n");
                })
                , async_read(client, server_data)
                    | unifex::then([&](std::size_t bytes_transferred)
                {
                    printf("[Server] Received %zu bytes.\n", bytes_transferred);
                })
                , async_write(client, server_data)
                    | unifex::then([&client](std::size_t bytes_transferred)
                {
                    printf("[Server] Sent %zu bytes.\n", bytes_transferred);
                    client.disconnect();
                })
                );
        });
    };

    auto client_logic = [&]()
    {
        auto endpoint = Endpoint_IPv4::from_string("127.0.0.1", 60260);
        assert(endpoint);

        return unifex::sequence(
            client_socket->async_connect(*endpoint)
                | unifex::then([]()
            {
                printf("[Client] Connected.\n");
            })
            , async_write(*client_socket, to_write, write_data.size())
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

    printf("Start using %zu buffers.\n", to_write.size());
    fflush(stdout);
    const auto start = std::chrono::steady_clock::now();

    IOCP_sync_wait(*iocp, unifex::when_all(server_logic(), client_logic()));

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    printf("Elapsed: %.3f seconds. %.3f MB/secs.\n"
        , seconds
        , (double(write_data.size()) / (1024 * 1024 * seconds)));
    fflush(stdout);

    assert(memcmp(write_data.data(), read_data.data(), write_data.size()) == 0);
    assert(memcmp(write_data.data(), server_data.data(), write_data.size()) == 0);
}
