add_library(deviced-rp1 STATIC
    completion.c
    ethernet.c
    dma.c
    interrupt.c
    resources.c
    publish.c
    rp1.c
    runtime.c
    runtime-source.c
    runtime-pci.c
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
