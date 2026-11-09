# include subdirectory for pci hosts
add_subdirectory(hosts)

add_library(deviced-pci STATIC
    windows.c
    assignment.c
    bars.c
    device.c
    discovery.c
    dma.c
    enumerate.c
    functionhandlers.c
    helpers.c
    interrupts.c
    msi.c
    msix.c
    msix-device.c
    host.c
    io.c
    publish.c
    resources.c
    strings.c
)

target_link_libraries(deviced-pci PRIVATE
    deviced-headers
    deviced-core
    deviced-firmware
    deviced-pci-hosts
    deviced-legacy
    libddk
    libds
    libos-api
    c
)
