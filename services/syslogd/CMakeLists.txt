enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_log_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(syslogd ""
    ${ADDITONAL_SOURCES}
    console/font.c
    console/fb.c
    console/log.c
    main.c
)
add_dependencies(syslogd service_servers)
deploy_file_to_initrd(syslogd "services" syslogd.yaml)
