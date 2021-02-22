if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_USBCOMMON)
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
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_protocol_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_protocol_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_protocol_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_protocol_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_protocol.h
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --include driver,usbhost,usbhub --server
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml
)

set (ADDITONAL_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_protocol_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_protocol_server.c)

add_library(usb-common STATIC
    ${ADDITONAL_SOURCES}

    manager.c
    scheduler.c
    scheduler_chain.c
    scheduler_periodic.c
    scheduler_settings.c
    service.c
    transfer.c
)
