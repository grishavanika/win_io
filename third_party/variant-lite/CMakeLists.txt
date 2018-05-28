
# Download and unpack variant-lite at configure time
configure_file(CMakeLists-variant-lite.txt.in variant-lite-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
	RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/variant-lite-download)
if(result)
	message(FATAL_ERROR "CMake step for variant-lite failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
	RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/variant-lite-download)
if(result)
	message(FATAL_ERROR "Build step for variant-lite failed: ${result}")
endif()

# Add variant-lite directly to our build
add_library(variant-lite INTERFACE)
target_include_directories(variant-lite INTERFACE
	${CMAKE_BINARY_DIR}/variant-lite-src/include)
