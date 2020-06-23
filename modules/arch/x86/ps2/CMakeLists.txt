if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_PS2)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../../librt/libgracht/include
    ../../../../librt/libusb/include
    ../../../../librt/libddk/include
    ../../../../librt/libds/include
    ../../../../librt/libc/include
    ../../../../librt/include
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --include driver --server
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hid_events_protocol_client.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/hid_protocol.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/hid_protocol.xml
)

set (ADDITONAL_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/hid_events_protocol_client.c)

add_module_target(ps2 ""
    ${ADDITONAL_SOURCES}
    devices/scancodes/scancodes2.c
    devices/keyboard.c
    devices/mouse.c

    main.c
    port.c
)
