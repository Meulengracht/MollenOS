# include subdirectory for pci hosts
add_subdirectory(hosts)

add_library(deviced-pci STATIC
    bars.c
    device.c
    discovery.c
    dma.c
    enumerate.c
    functionhandlers.c
    helpers.c
    interrupts.c
    host.c
    io.c
    resources.c
    strings.c
)

# PCI selects RP1 handlers, which in turn call the PCI interfaces.
target_link_libraries(deviced-pci PRIVATE
    deviced-headers
    deviced-core
    deviced-firmware
    deviced-pci-hosts
    deviced-legacy
    deviced-rp1
    libddk
    libds
    libos-api
    c
)
