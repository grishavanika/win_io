set(exe_name fs_changes_print)

target_collect_sources(${exe_name})

add_executable(${exe_name} ${${exe_name}_files})
set_all_warnings(${exe_name} PRIVATE)
target_link_libraries(${exe_name} PRIVATE win_io)

# Enable Unicode Character Set
target_compile_definitions(${exe_name} PRIVATE
    UNICODE _UNICODE)
