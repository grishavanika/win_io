set(exe_name test_win_io)

target_collect_sources(${exe_name})

add_executable(${exe_name} ${${exe_name}_files})

set_all_warnings(${exe_name} PUBLIC)

target_link_libraries(${exe_name} PRIVATE win_io)
target_link_libraries(${exe_name} PRIVATE gtest_all)
