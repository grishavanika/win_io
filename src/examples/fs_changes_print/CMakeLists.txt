set(exe_name fs_changes_print)

if (TARGET date_Integrated)
    target_collect_sources(${exe_name})
    
    add_executable(${exe_name} ${${exe_name}_files})

    # Enable Unicode Character Set
    target_compile_definitions(${exe_name} PRIVATE
        UNICODE _UNICODE)

    if (${gcc})
        # Make GCC aware about wmain()
        set_target_properties(${exe_name} PROPERTIES LINK_FLAGS -municode)
    endif()

    set_all_warnings(${exe_name} PRIVATE)

    target_link_libraries(${exe_name} PRIVATE win_io)
    target_link_libraries(${exe_name} PRIVATE date_Integrated)
else()
    message("[x] No date library found. Skipping fs_changes_print.")
endif()
