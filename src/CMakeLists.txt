
add_subdirectory(win_io)

if (${has_coro_support})
	add_subdirectory(win_io_coro)
endif()

add_subdirectory(tests)
add_subdirectory(utils)
