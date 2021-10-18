# GoogleTest integration
include(FetchContent)
include(CMakePrintHelpers)

macro(find_gtest found_gtest)

    find_package(GTest)
    if (GTEST_FOUND)
        add_library(GTest_Integrated INTERFACE)
        target_link_libraries(GTest_Integrated INTERFACE GTest::gtest GTest::gmock GTest::gtest_main)

        print_target_source_dir(GTest::gtest)
        print_target_source_dir(GTest::gmock)
        print_target_source_dir(GTest::gtest_main)

        set(${found_gtest} ON)
        if (MSVC)
            target_compile_options(GTest_Integrated INTERFACE
                # class needs to have dll-interface
                /wd4251
                # non-DLL-interface used as base for DLL-interface
                /wd4275)
        endif ()
    endif ()


endmacro()

macro(setup_gtest_from_git)

    # This seems useless (FULLY_DISCONNECTED ON)
    # but this way CMake will not REBUILD the project
    # every time after generation.
    FetchContent_Declare(
        gtest
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/gtest-src"
        FULLY_DISCONNECTED ON)
    FetchContent_GetProperties(gtest)
    if (NOT gtest_POPULATED)
        FetchContent_Populate(gtest)
    endif ()
    cmake_print_variables(gtest_SOURCE_DIR)

    # GoogleTest uses static C++ runtime by default,
    # use C++ as dll instead.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(${gtest_SOURCE_DIR} ${gtest_BINARY_DIR} EXCLUDE_FROM_ALL)

    print_target_source_dir(gmock_main)
    print_target_source_dir(gmock)
    print_target_source_dir(gtest)

    # gmock_main compiles GTest and GMock, but misses GTest include directories
    # when using FetchContent* with predefined SOURCE_DIR that
    # points to ${ROOT}/third_party/deps/...
    target_include_directories(gmock_main PUBLIC $<BUILD_INTERFACE:${gtest_SOURCE_DIR}/googletest>)
    target_include_directories(gmock PUBLIC      $<BUILD_INTERFACE:${gtest_SOURCE_DIR}/googletest>)
    target_include_directories(gmock_main PUBLIC $<BUILD_INTERFACE:${gtest_SOURCE_DIR}/googletest/include>)
    target_include_directories(gmock PUBLIC      $<BUILD_INTERFACE:${gtest_SOURCE_DIR}/googletest/include>)

    if (MSVC)
        target_compile_definitions(gmock_main PUBLIC
            -D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
        target_compile_definitions(gmock_main PRIVATE
            _CRT_SECURE_NO_WARNINGS)
    endif ()

    # Disable overloads for std::tr1::tuple type.
    target_compile_definitions(gmock PUBLIC
        -DGTEST_HAS_TR1_TUPLE=0)

    if (clang_on_msvc)
        target_compile_options(gmock PUBLIC
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
            -Wno-unused-parameter
            -Wno-deprecated)
    endif ()

    add_library(GTest_Integrated INTERFACE)
    target_link_libraries(GTest_Integrated INTERFACE gmock)

    set_target_properties(gmock PROPERTIES FOLDER third_party)
    set_target_properties(gtest PROPERTIES FOLDER third_party)

endmacro()

set(found_gtest OFF)
if (only_msvc)
    message("[ ] Trying to integrate GTest/GMock with find_package().")
    find_gtest(found_gtest)
    if (NOT found_gtest)
        message("[x] Failed to find GTest/GMock.")
    endif ()
endif ()

if (NOT found_gtest)
    message("[ ] Integrating GTest/GMock from git.")
    setup_gtest_from_git()
endif ()
