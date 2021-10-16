set(exe_name async_TCP_IOCP_senders_receivers)

if (TARGET unifex_Integrated)
    target_collect_sources(${exe_name})
    
    add_executable(${exe_name} ${${exe_name}_files})
    
    set_all_warnings(${exe_name} PRIVATE)
    
    target_link_libraries(${exe_name} PRIVATE win_io)
    target_link_libraries(${exe_name} PUBLIC unifex_Integrated)
else()
    message("[x] No unifex library found. Skipping async_TCP_IOCP_senders_receivers.")
endif()
