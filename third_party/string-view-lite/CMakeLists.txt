
# Download and unpack string-view-lite at configure time
configure_file(CMakeLists-string-view-lite.txt.in string-view-lite-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
	RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/string-view-lite-download)
if(result)
	message(FATAL_ERROR "CMake step for string-view-lite failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
	RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/string-view-lite-download)
if(result)
	message(FATAL_ERROR "Build step for string-view-lite failed: ${result}")
endif()

# Add string-view-lite directly to our build
add_library(string-view-lite INTERFACE)
target_include_directories(string-view-lite INTERFACE
	${CMAKE_BINARY_DIR}/string-view-lite-src/include)
