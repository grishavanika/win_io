
add_subdirectory(win_io)
add_subdirectory(win_socks)

if (${has_coro_support})
	add_subdirectory(win_io_coro)
endif()

add_subdirectory(tests)
add_subdirectory(examples)
