enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
)

set (ADDITONAL_SOURCES
        ${CMAKE_BINARY_DIR}/protocols/sys_usb_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(usbd ""
        ${ADDITONAL_SOURCES}

        port_event.c
        controllers.c
        hubs.c
        devices.c
        main.c
)
add_dependencies(usbd service_servers)
deploy_file_to_initrd(usbd "services" usbd.yaml)
