if (VALI_BUILD_UNIT_TESTS)
    # add all targets that support unit testing
    add_subdirectory (vfs)
    return ()
endif ()

# include sub projects
add_subdirectory(vfs)

set (SERVICE_HEADERS
        include/vfs/filesystem.h
        include/vfs/gpt.h
        include/vfs/mbr.h
        include/vfs/scope.h
        include/vfs/storage.h
        include/vfs/vfs.h
)

set (SERVICE_SOURCES
        interfaces/driver_ops.c
        interfaces/modules.c

        storage/layouts/detect.c
        storage/layouts/mbr.c
        storage/layouts/gpt.c
        storage/storage.c
        storage/storage_device.c
        storage/storage_file.c
        storage/storage_memory.c
        storage/utils.c

        api.c
        filesystem.c
        main.c
        mount.c
        scope.c
)

set (ADDITONAL_SOURCES
        ${CMAKE_BINARY_DIR}/protocols/sys_file_service_server.c
        ${CMAKE_BINARY_DIR}/protocols/sys_mount_service_server.c
        ${CMAKE_BINARY_DIR}/protocols/sys_storage_service_server.c
        ${CMAKE_BINARY_DIR}/protocols/ctt_filesystem_service_client.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(filed "-D__FILEMANAGER_IMPL"
        ${SERVICE_HEADERS}
        ${SERVICE_SOURCES}
        ${ADDITONAL_SOURCES}
)
target_link_libraries(filed PRIVATE filed-vfs)
target_include_directories(filed PRIVATE
        ${CMAKE_BINARY_DIR}/protocols
        ${CMAKE_CURRENT_BINARY_DIR}
        include
)
add_dependencies(filed service_servers)
deploy_file_to_initrd(filed "services" filed.yaml)
