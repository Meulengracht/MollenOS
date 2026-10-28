add_library(deviced-platform STATIC
    activation.c
    dma.c
    interrupts.c
    enumerate.c
    ownership.c
    platform.c
    discovery.c
)

target_link_libraries(deviced-platform PRIVATE
    deviced-headers
    deviced-core
    deviced-firmware
    libddk
    libos-api
    c
)
