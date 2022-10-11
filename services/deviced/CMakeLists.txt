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

add_service_target(deviced ""
        ${ADDITONAL_SOURCES}

        discover/configparser.c
        discover/discover.c
        discover/initrd.c

        devices.c
        main.c
        requests.c
)
target_include_directories(deviced PRIVATE include)
target_link_libraries(deviced PRIVATE libyaml vafs)
add_dependencies(deviced service_servers contract_clients)
deploy_file_to_initrd(deviced "services" deviced.yaml)
