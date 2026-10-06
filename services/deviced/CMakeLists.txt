enable_language(C)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_device_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_client.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

include_directories(bus/pci)

add_service_target(deviced ""
        ${ADDITONAL_SOURCES}

        bus/legacy/fixed.c

        bus/pci/hosts/ecam.c
        bus/pci/hosts/legacy.c
        bus/pci/hosts/broadcom/bcm.c
        bus/pci/hosts/broadcom/bcm2711.c
        bus/pci/hosts/broadcom/bcm2712.c
        bus/pci/hosts/broadcom/firmware.c

        bus/pci/bars.c
        bus/pci/device.c
        bus/pci/functionhandlers.c
        bus/pci/interrupts.c
        bus/pci/pcienumerate.c
        bus/pci/pcihelpers.c
        bus/pci/pciio.c
        bus/pci/pcistrings.c
        bus/pci/resources.c

        bus/rp1/firmware.c
        bus/rp1/interrupt.c
        bus/rp1/publish.c
        bus/rp1/rp1.c

        core/configparser.c
        core/discover.c
        core/match.c
        core/initrd.c
        core/devices.c
        core/requests.c

        firmware/fdt.c
        firmware/gic.c
        firmware/pci.c
        firmware/resources.c

        main.c
)
target_include_directories(deviced PRIVATE include .)
target_link_libraries(deviced PRIVATE libyaml vafs vali-fdt)
add_dependencies(deviced service_servers contract_clients)
deploy_file_to_initrd(deviced "services" deviced.yaml)
