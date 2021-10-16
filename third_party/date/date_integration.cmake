# date integration

message("[ ] Trying to integrate date with find_package().")
find_package(date CONFIG)
if (date_FOUND)
    print_target_source_dir(date::date)
    add_library(date_Integrated INTERFACE)
    target_link_libraries(date_Integrated INTERFACE date::date)
else ()
    message("[x] No date found.")
endif ()

