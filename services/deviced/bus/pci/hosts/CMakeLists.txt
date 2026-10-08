add_library(deviced-pci-hosts STATIC
    ecam.c
    legacy.c
    broadcom/bcm.c
    broadcom/bcm2711.c
    broadcom/bcm2712.c
    broadcom/firmware.c
)

target_link_libraries(deviced-pci-hosts PRIVATE
    deviced-headers
    deviced-firmware
    vali-fdt-headers
    libddk
    libds
    libos-api
    c
)
