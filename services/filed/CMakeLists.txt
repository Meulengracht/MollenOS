enable_language(C)

# include sub projects
add_subdirectory(vfs)

set (SERVICE_HEADERS
        include/vfs/filesystem.h
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

        filesystem.c
        modules/modules.c
        main.c
        requests.c
        scope.c
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_file_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_storage_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(filemanager "-D__FILEMANAGER_IMPL"
        ${SERVICE_HEADERS}
        ${SERVICE_SOURCES}
        ${ADDITONAL_SOURCES}
)
target_link_libraries(filemanager PRIVATE filed-vfs)
target_include_directories(filemanager PRIVATE
        ${CMAKE_BINARY_DIR}/protocols
        ${CMAKE_CURRENT_BINARY_DIR}
        include
)
add_dependencies(filemanager service_servers)
deploy_file_to_initrd(filemanager "services" filemanager.yaml)
