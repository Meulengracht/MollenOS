set(ADDITONAL_SOURCES
        ${CMAKE_BINARY_DIR}/protocols/sys_file_service_client.c
        ${CMAKE_BINARY_DIR}/protocols/ctt_storage_service_client.c
        ${CMAKE_BINARY_DIR}/protocols/ctt_filesystem_service_server.c
)
set_source_files_properties(${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE)

add_library(fs-common STATIC
        ${ADDITONAL_SOURCES}
        requests.c
        service.c
        storage.c
        wrappers.c
)
target_include_directories(fs-common PUBLIC include)
target_link_libraries(fs-common PUBLIC libos-api gracht libddk libds c)
