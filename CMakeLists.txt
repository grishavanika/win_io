cmake_minimum_required(VERSION 3.12.4 FATAL_ERROR)
# 3.12.4 is required because of C++20 support.
project(win_io)

include(cmake_utils.cmake)
include(cmake_clang_msvc.cmake)
include(CTest)

# Enable folders for VS
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# Leave only most Debug/Release configurations
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

detect_compilers()
set_cpp20_standard()
detect_compiler_features()

if (clang_on_msvc)
    make_clang_work_on_windows()
endif()

# ----------------------------- DEPS
# unifex integration
include(FetchContent)
include(CMakePrintHelpers)

FetchContent_Declare(
  unifex
  GIT_REPOSITORY https://github.com/facebookexperimental/libunifex.git
  GIT_TAG        af981be0d4ecdcfc174a2aa5f2e61d917accba0e
)

FetchContent_MakeAvailable(unifex)
# cmake_print_variables(unifex_SOURCE_DIR)

print_target_source_dir(unifex)
set_target_properties(unifex PROPERTIES FOLDER third_party)

if (clang_on_msvc)
    target_compile_definitions(unifex PUBLIC
        UNIFEX_NO_COROUTINES)
endif(
)
add_library(unifex_Integrated INTERFACE)
target_link_libraries(unifex_Integrated INTERFACE unifex)

if (clang_on_msvc)
    target_compile_options(unifex_Integrated INTERFACE
        -Wno-documentation-unknown-command
        -Wno-shadow-field-in-constructor)
endif ()

if (MSVC)
    target_compile_options(unifex_Integrated INTERFACE
        # declaration of 'receiver' hides global declaration
        /wd4459
        )
endif ()

# gtest integration
find_package(GTest CONFIG REQUIRED)
add_library(GTest_Integrated INTERFACE)
target_link_libraries(GTest_Integrated INTERFACE GTest::gtest GTest::gmock GTest::gtest_main)

# -----------------------------

add_subdirectory(src)

