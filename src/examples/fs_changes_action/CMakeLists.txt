set(exe_name fs_changes_action)

if (TARGET rxcpp)
    target_collect_sources(${exe_name})
    
    add_executable(${exe_name} WIN32 ${${exe_name}_files})
    
    # Enable Unicode Character Set
    target_compile_definitions(${exe_name} PRIVATE
        UNICODE _UNICODE)
    
    if (${gcc})
        # Make GCC aware about wmain()
        set_target_properties(${exe_name} PROPERTIES LINK_FLAGS -municode)
    endif()
    
    set_all_warnings(${exe_name} PRIVATE)
    
    target_link_libraries(${exe_name} PRIVATE win_io)
    target_link_libraries(${exe_name} PUBLIC rxcpp)
else()
    message("[x] No RxCpp library found. Skipping fs_changes_action.")
endif()
