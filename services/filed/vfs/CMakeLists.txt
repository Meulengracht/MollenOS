if (VALI_BUILD_UNIT_TESTS)
    set (VFS_INCLUDES
            ${CMAKE_SOURCE_DIR}/librt/libgracht/include
            ${CMAKE_SOURCE_DIR}/librt/libddk/include
            ${CMAKE_SOURCE_DIR}/librt/libds/include
            ${CMAKE_SOURCE_DIR}/librt/libos/include
            ../include
    )

    add_unit_test(FILE utils_test.c
            INCLUDES ${VFS_INCLUDES}
            DEFINES "-D__FILEMANAGER_IMPL"
            LIBS mstring
    )

    return ()
endif ()

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
