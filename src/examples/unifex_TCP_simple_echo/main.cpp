#include "unifex_IOCP_sockets.hpp"

#include <chrono>

#include <cstring>

int main()
{
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

    const auto start = std::chrono::steady_clock::now();

    IOCP_sync_wait(*iocp, unifex::when_all(server_logic(), client_logic()));

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    printf("Elapsed: %.3f seconds. %.3f MB/secs.\n"
        , seconds
        , (double(write_data.size()) / (1024 * 1024 * seconds)));

    assert(memcmp(write_data.data(), read_data.data(), write_data.size()) == 0);
    assert(memcmp(write_data.data(), server_data.data(), write_data.size()) == 0);
}
