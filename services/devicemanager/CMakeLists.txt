if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiDeviceManager)
endif ()

enable_language(C)

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
        discover/parser.c
        discover/discover.c

        devices.c
        main.c
        requests.c
)
target_include_directories(devicemanager PRIVATE include)
target_link_libraries(devicemanager libyaml vafs libds libddk librt gracht_static libc libm)
add_dependencies(devicemanager service_servers contract_clients)
deploy_file_to_initrd(devicemanager "services" devicemanager.yaml)
