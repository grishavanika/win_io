
# Download and unpack optional-lite at configure time
configure_file(CMakeLists-optional-lite.txt.in optional-lite-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
	RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/optional-lite-download)
if(result)
	message(FATAL_ERROR "CMake step for optional-lite failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
	RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/optional-lite-download)
if(result)
	message(FATAL_ERROR "Build step for optional-lite failed: ${result}")
endif()

# Add optional-lite directly to our build
add_library(optional-lite INTERFACE)
target_include_directories(optional-lite INTERFACE
	${CMAKE_BINARY_DIR}/optional-lite-src/include)
