target_collect_sources(win_io)

set(win_io_FILES
    include/win_io/io_completion_port.h
    include/win_io/read_directory_changes.h
    )
add_library(win_io INTERFACE ${win_io_FILES})
target_include_directories(win_io INTERFACE include)
