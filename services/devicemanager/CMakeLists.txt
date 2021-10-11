if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiDeviceManager)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_device_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_client.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

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
add_dependencies(devicemanager service_servers contract_clients)
