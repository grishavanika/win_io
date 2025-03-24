add_executable(unifex_TCP_connect "main.cpp")
set_all_warnings(unifex_TCP_connect PRIVATE)
target_link_libraries(unifex_TCP_connect PRIVATE win_socks)
