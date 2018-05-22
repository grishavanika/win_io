# GoogleTest integration

# Using macro since most of directives should be repeated 3 times
# (for gtest, gmock_main and gmock). Using target_compile*() command
# with PUBLIC argument does not help (#TODO: investigate why)
macro(setup_gtest_lib lib_name)
	if (MSVC)
		target_compile_definitions(${lib_name} PUBLIC
			-D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
	endif()

	# Disable overloads for std::tr1::tuple type
	target_compile_definitions(${lib_name} PUBLIC
		-DGTEST_HAS_TR1_TUPLE=0)

	if (clang_on_msvc)
		target_compile_options(${lib_name} PUBLIC
			-Wno-undef
			-Wno-exit-time-destructors
			-Wno-format-nonliteral
			-Wno-missing-prototypes
			-Wno-missing-noreturn
			-Wno-shift-sign-overflow
			-Wno-used-but-marked-unused
			-Wno-nonportable-system-include-path
			-Wno-missing-variable-declarations
			-Wno-covered-switch-default
			-Wno-unused-member-function
			-Wno-switch-enum
			-Wno-deprecated)
	endif()

	set_target_properties(${lib_name} PROPERTIES FOLDER third_party)

endmacro()

macro(setup_gtest_manually)
	# GoogleTest uses static C++ runtime by default,
	# use C++ as dll instead
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

	add_subdirectory(gtest)

	setup_gtest_lib(gtest)
	setup_gtest_lib(gmock_main)
	setup_gtest_lib(gmock)

	add_library(gtest_all INTERFACE)
	target_link_libraries(gtest_all INTERFACE gtest gmock_main)
endmacro()

if (MSVC)
	# Assume that Clang and GCC for Windows both have no pre-built libraries
	# (Like with vcpkg)
	find_package(GTest)
endif()

if (${GTest_FOUND})
	add_library(gtest_all INTERFACE)
	target_link_libraries(gtest_all INTERFACE GTest::GTest GTest::Main)
	if (MSVC)
		target_compile_options(gtest_all INTERFACE
			# class needs to have dll-interface to be used by clients
			/wd4251
			# non–DLL-interface used as base for DLL-interface
			/wd4275)
	endif()
else()
	setup_gtest_manually()
endif()

