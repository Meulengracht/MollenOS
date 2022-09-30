add_filesystem_target(fs-common STATIC
        storage.c
)
target_include_directories(fs-common PUBLIC include)
target_link_libraries(fs-common PUBLIC gracht libddk c)
