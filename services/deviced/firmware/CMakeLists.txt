add_library(deviced-firmware STATIC
    bcm.c
    dma.c
    ethernet.c
    fdt.c
    gic.c
    mapping.c
    pci.c
    platform.c
    platform-runtime.c
    resources.c
    rp1.c
)

# Only this target consumes vali-fdt's parser source.
target_link_libraries(deviced-firmware PRIVATE
    deviced-headers
    vali-fdt
    libddk
    libos-api
    c
)
