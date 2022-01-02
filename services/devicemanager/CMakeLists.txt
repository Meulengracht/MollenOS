if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
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
        include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_device_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_client.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

if (${VALI_ARCH} STREQUAL "i386" OR ${VALI_ARCH} STREQUAL "amd64")
    include_directories(bus/pci)
    set (ADDITONAL_SOURCES
        ${ADDITONAL_SOURCES}
        bus/pci/pcienumerate.c
        bus/pci/pcihelpers.c
        bus/pci/pciio.c
        bus/pci/pcistrings.c
    )
endif ()

add_service_target(devicemanager ""
        ${ADDITONAL_SOURCES}

        discover/ramdisk.c
        discover/discover.c

        devices.c
        main.c
        requests.c
)
add_dependencies(devicemanager service_servers contract_clients)
deploy_to_initrd(devicemanager ON)
