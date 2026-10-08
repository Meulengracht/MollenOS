add_library(deviced-rp1 STATIC
    firmware.c
    interrupt.c
    publish.c
    rp1.c
)

target_link_libraries(deviced-rp1 PRIVATE
    deviced-headers
    deviced-core
    deviced-pci
    deviced-firmware
    vali-fdt-headers
    libddk
    libds
    libos-api
    c
)
