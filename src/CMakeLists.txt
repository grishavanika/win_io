
add_subdirectory(win_io)
add_subdirectory(win_socks)

if (${has_coro_support})
	add_subdirectory(win_io_coro)
endif()

# tests ---------------

add_subdirectory(test_win_io)
set_target_properties(test_win_io PROPERTIES FOLDER tests)
new_test(win_io test_win_io)

if (${has_coro_support})
	add_subdirectory(test_win_io_coro)
	set_target_properties(test_win_io_coro PROPERTIES FOLDER tests)
	new_test(win_io_coro test_win_io_coro)
endif()

# examples ---------------

add_subdirectory(fs_changes_print)
add_subdirectory(rxcpp_fs_changes)
add_subdirectory(unifex_TCP_connect)
add_subdirectory(unifex_TCP_simple_echo)
add_subdirectory(unifex_tcp_echo_server)
add_subdirectory(unifex_tcp_echo_client)
add_subdirectory(unifex_udp_echo_server)
add_subdirectory(unifex_udp_echo_client)
set_target_properties(fs_changes_print PROPERTIES FOLDER examples)
set_target_properties(rxcpp_fs_changes PROPERTIES FOLDER examples)
set_target_properties(unifex_TCP_connect PROPERTIES FOLDER examples)
set_target_properties(unifex_TCP_simple_echo PROPERTIES FOLDER examples)
set_target_properties(unifex_tcp_echo_server PROPERTIES FOLDER examples)
set_target_properties(unifex_tcp_echo_client PROPERTIES FOLDER examples)
set_target_properties(unifex_udp_echo_server PROPERTIES FOLDER examples)
set_target_properties(unifex_udp_echo_client PROPERTIES FOLDER examples)
