if (TARGET win_socks)
    add_executable(async_tcp_echo_client "main.cpp")
    set_all_warnings(async_tcp_echo_client PRIVATE)
    target_link_libraries(async_tcp_echo_client PRIVATE win_socks)
else()
    message("[x] No win_socks library found. Skipping async_tcp_echo_client.")
endif()
