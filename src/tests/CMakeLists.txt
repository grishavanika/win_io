include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(test_win_io)
add_subdirectory(test_win_io_coro)
set_target_properties(test_win_io PROPERTIES FOLDER tests)
set_target_properties(test_win_io_coro PROPERTIES FOLDER tests)

new_test(win_io test_win_io)
new_test(win_io_coro test_win_io_coro)
