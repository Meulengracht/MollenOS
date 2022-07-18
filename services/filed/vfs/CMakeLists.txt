# add sublibraries
add_subdirectory(handlers)

add_library(filed-vfs
    handles.c
    memfs.c
    mount.c
    utils.c
    vfs.c
)
set_target_properties (filed-vfs
        PROPERTIES
        COMPILE_FLAGS "-Wno-address-of-packed-member -D__SERVICE_IMPL -D__FILEMANAGER_IMPL"
)
target_link_libraries(filed-vfs PUBLIC filed-vfs-handlers)

# add unit tests
get_target_property(VFS_INCLUDES filed-vfs-handlers INCLUDE_DIRECTORIES)
add_unit_test(FILE utils_test.c INCLUDES ${VFS_INCLUDES} DEFINES "-D__FILEMANAGER_IMPL")
