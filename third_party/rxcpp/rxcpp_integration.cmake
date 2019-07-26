# RxCpp integration

find_path(rxcpp_INCLUDE_DIR
	NAMES rxcpp/rx.hpp 
	DOC "rxcpp library header files")

if (EXISTS "${rxcpp_INCLUDE_DIR}")
	message("Found RxCpp at ${rxcpp_INCLUDE_DIR}")
	add_library(rxcpp INTERFACE)
	target_include_directories(rxcpp INTERFACE ${rxcpp_INCLUDE_DIR})

	target_compile_options(rxcpp INTERFACE 
		# unreachable code
		/wd4702)

else ()
	message(FATAL_ERROR "No rxcpp found")
endif ()
