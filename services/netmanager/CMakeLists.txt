if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiNetManager)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
    ../../librt/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_socket_protocol_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(netmanager ""
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
add_dependencies(netmanager service_servers)
