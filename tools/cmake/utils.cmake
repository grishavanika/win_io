include(CMakePrintHelpers)

macro(set_cpp17_standard)
	set(CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_STANDARD 17)

	# And `CMAKE_CXX_STANDARD` does not work for Clang on Windows.
	# Also, MSVC's /std switch needs to be set
	# (for proper detection of _HAS_CXX17 in std headers)
	if (clang_on_msvc)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z /std:c++17")
	endif()
endmacro()

macro(detect_compilers)
	set(clang_on_msvc OFF)
	set(clang OFF)
	set(only_msvc ${MSVC})
	if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
		set(gcc ON)
	endif()

	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		set(clang ON)
		set(clang_on_msvc ${MSVC})
		set(only_msvc OFF)
	endif()

	cmake_print_variables(clang_on_msvc clang only_msvc gcc)
endmacro()

macro(detect_compiler_features)
	set(has_coro_support ${only_msvc})

	cmake_print_variables(has_coro_support clang only_msvc gcc)
endmacro()

macro(set_all_warnings target visibility)
	if (only_msvc)
		target_compile_options(${target} ${visibility}
			/W4 /WX)

		# Be more close to Standard. Enable two-phase lookup
		target_compile_options(${target} ${visibility}
			/permissive-)
	endif()

	if (NOT MSVC)
		target_compile_options(${target} ${visibility}
			-Wall -Wextra -Wpedantic -Werror)
	endif()

	if (clang_on_msvc)
		target_compile_options(${target} ${visibility}
			-Wall -Wextra -Werror)
	endif()
endmacro()

# set PCH for VS project
# https://stackoverflow.com/questions/148570/using-pre-compiled-headers-with-cmake

# Note: relies on ${only_msvc} global variable
function(set_target_precompiled_header target pc_header pc_source)
  if (only_msvc)
     set_target_properties(${target} PROPERTIES COMPILE_FLAGS "/Yu${pc_header}")
     set_source_files_properties(${pc_source} PROPERTIES COMPILE_FLAGS "/Yc${pc_header}")
  endif ()
endfunction()

# Note: relies on ${bin_folders} global variable
function(target_install_lib_binaries target)
	if(${BUILD_SHARED_LIBS})
		foreach(config ${CMAKE_CONFIGURATION_TYPES})
			install(TARGETS ${target}
				RUNTIME DESTINATION bin/${config}/${bin_folder}
				CONFIGURATIONS ${config})
		endforeach(config)
	endif()
endfunction()

function(target_install_binaries target)
	foreach(config ${CMAKE_CONFIGURATION_TYPES})
		install(TARGETS ${target}
			RUNTIME DESTINATION bin/${config}
			CONFIGURATIONS ${config})
	endforeach(config)
endfunction()


macro(target_collect_sources target)

	file(GLOB_RECURSE
		${target}_files
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"*.cpp" "*.h" "*.inl")

endmacro()

macro(new_test name command)
	get_filename_component(base_install_folder ${CMAKE_INSTALL_PREFIX} ABSOLUTE)

	foreach(config ${CMAKE_CONFIGURATION_TYPES})
		set(install_folder ${base_install_folder}/bin/${config}/tests)

		add_test(NAME
			${name}_${config}
			COMMAND ${command}
			CONFIGURATIONS ${config}
			WORKING_DIRECTORY ${install_folder})
	endforeach()

endmacro()


