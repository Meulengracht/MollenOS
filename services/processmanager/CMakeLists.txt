enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_library_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_process_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(processmanager ""
    ${ADDITONAL_SOURCES}

    debugger.c
    main.c
    map_parser.c
    process.c
    requests.c
    symbol_loader.c
)
add_dependencies(processmanager service_servers)
