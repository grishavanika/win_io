set(lib_name win_io_coro)

target_collect_sources(${lib_name})

add_library(${lib_name} ${${lib_name}_files})

set_all_warnings(${lib_name} PUBLIC)

target_compile_definitions(${lib_name} PRIVATE
    VC_EXTRALEAN WIN32_LEAN_AND_MEAN NOMINMAX)

target_include_directories(${lib_name} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(${lib_name} PUBLIC include)

target_link_libraries(${lib_name} PUBLIC win_io)
