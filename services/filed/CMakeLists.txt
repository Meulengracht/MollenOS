enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
    include
)

set (SERVICE_HEADERS
        include/vfs/filesystem.h
        include/vfs/filesystem_module.h
        include/vfs/filesystem_types.h
        include/vfs/gpt.h
        include/vfs/mbr.h
        include/vfs/requests.h
        include/vfs/scope.h
        include/vfs/storage.h
        include/vfs/vfs.h
)

set (SERVICE_SOURCES
        storage/layouts/detect.c
        storage/layouts/mbr.c
        storage/layouts/gpt.c

        storage/storage.c
        storage/utils.c

        vfs/handlers/close.c
        vfs/handlers/duplicate.c
        vfs/handlers/flush.c
        vfs/handlers/handle_stat.c
        vfs/handlers/link.c
        vfs/handlers/move.c
        vfs/handlers/open.c
        vfs/handlers/read.c
        vfs/handlers/readlink.c
        vfs/handlers/realpath.c
        vfs/handlers/seek.c
        vfs/handlers/stat.c
        vfs/handlers/unlink.c
        vfs/handlers/write.c
        vfs/api.c
        vfs/handles.c
        vfs/utils.c
        vfs/vfs.c

        filesystem.c
        handle.c
        modules.c
        main.c
        requests.c
        scope.c
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_file_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_path_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_storage_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(filemanager "-D__FILEMANAGER_IMPL"
        ${SERVICE_HEADERS}
        ${SERVICE_SOURCES}
        ${ADDITONAL_SOURCES}
)
add_dependencies(filemanager service_servers)
deploy_file_to_initrd(filemanager "services" filemanager.yaml)
