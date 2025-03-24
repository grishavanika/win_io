# unifex integration
include(FetchContent)
include(CMakePrintHelpers)

macro(setup_unifex_from_git)

    # This seems useless (FULLY_DISCONNECTED ON)
    # but this way CMake will not REBUILD the project
    # every time after generation.
    FetchContent_Declare(
        unifex
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/unifex-src"
        FULLY_DISCONNECTED ON)
    FetchContent_GetProperties(unifex)
    if (NOT unifex_POPULATED)
        FetchContent_Populate(unifex)
    endif ()
    cmake_print_variables(unifex_SOURCE_DIR)

    add_subdirectory(${unifex_SOURCE_DIR} ${unifex_BINARY_DIR} EXCLUDE_FROM_ALL)

    print_target_source_dir(unifex)
    set_target_properties(unifex PROPERTIES FOLDER third_party)

    if (clang_on_msvc)
        target_compile_definitions(unifex PUBLIC
            UNIFEX_NO_COROUTINES)
    endif()

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

endmacro()


message("[ ] Integrating unifex from git.")
setup_unifex_from_git()
