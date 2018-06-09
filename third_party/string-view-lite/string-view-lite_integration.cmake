# string-view-lite integration

add_subdirectory(string-view-lite)

if (clang_on_msvc)
	target_compile_options(string-view-lite INTERFACE
		-Wno-undef)
endif()
