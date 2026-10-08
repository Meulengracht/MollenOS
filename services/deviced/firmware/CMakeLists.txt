add_library(deviced-firmware STATIC
    fdt.c
    gic.c
    pci.c
    resources.c
)

# Firmware decoding calls the Broadcom helpers supplied by the host library.
# Only this target consumes vali-fdt's parser source.
target_link_libraries(deviced-firmware PRIVATE
    deviced-headers
    deviced-pci-hosts
    vali-fdt
    libddk
    libos-api
    c
)
