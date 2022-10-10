enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_session_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(sessiond ""
    ${ADDITONAL_SOURCES}
    
    main.c
)
add_dependencies(sessiond service_servers)
deploy_file_to_initrd(sessiond "services" sessiond.yaml)
