enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_socket_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(netd ""
    ${ADDITONAL_SOURCES}
    domains/bt.c
    domains/domains.c
    domains/internet.c
    domains/local.c
    domains/unspec.c

    manager.c
    socket.c
    main.c
)
add_dependencies(netd service_servers)
deploy_file_to_initrd(netd "services" netd.yaml)
