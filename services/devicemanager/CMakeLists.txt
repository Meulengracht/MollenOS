if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiDeviceManager)
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
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/svc_device_protocol_server.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml --out ${CMAKE_CURRENT_BINARY_DIR} --include device --lang-c --server
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml
)

set (ADDITONAL_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/svc_device_protocol_server.c)

if (${VALI_ARCH} STREQUAL "i386" OR ${VALI_ARCH} STREQUAL "amd64")
    include_directories(arch/x86)
    set (ADDITONAL_SOURCES
        ${ADDITONAL_SOURCES}
        arch/x86/pcienumerate.c
        arch/x86/pcihelpers.c
        arch/x86/pciio.c
        arch/x86/pcistrings.c
    )
endif ()

add_service_target(devicemanager "" main.c ${ADDITONAL_SOURCES})
