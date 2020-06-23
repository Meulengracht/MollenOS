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

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/svc_socket_protocol_server.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --include socket --server
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml
)

set (ADDITONAL_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/svc_socket_protocol_server.c)

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
