include(CMakePrintHelpers)

set(exe_name fs_changes_action)

target_collect_sources(${exe_name})

add_executable(${exe_name} WIN32 ${${exe_name}_files})

# Enable Unicode Character Set
target_compile_definitions(${exe_name} PRIVATE
    UNICODE _UNICODE)

if (${gcc})
    # Make GCC aware about wmain()
    set_target_properties(${exe_name} PROPERTIES LINK_FLAGS -municode)
endif()

set_all_warnings(${exe_name} PRIVATE)

target_link_libraries(${exe_name} PRIVATE win_io)

find_package(rxcpp CONFIG REQUIRED) # vcpkg
target_link_libraries(${exe_name} PUBLIC rxcpp)

# vcpkg rxcpp seems to be broken, find headers manually:
find_path(rxcpp_INCLUDE_DIR
    NAMES rxcpp/rx.hpp 
    DOC "rxcpp library header files")
cmake_print_variables(rxcpp_INCLUDE_DIR)
target_include_directories(rxcpp INTERFACE ${rxcpp_INCLUDE_DIR})
