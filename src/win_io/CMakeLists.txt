set(lib_name win_io)

target_collect_sources(${lib_name})

add_library(${lib_name} ${${lib_name}_files})

set_all_warnings(${lib_name} PUBLIC)

# Turn-off not-needed Windows.h parts.
# See http://web.archive.org/web/20121219084749/http://support.microsoft.com/kb/166474
# for more details
target_compile_definitions(${lib_name} PRIVATE
	VC_EXTRALEAN WIN32_LEAN_AND_MEAN NOMINMAX)

target_include_directories(${lib_name} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(${lib_name} PUBLIC include)

