add_library(deviced-legacy STATIC
    fixed.c
)

target_link_libraries(deviced-legacy PRIVATE
    deviced-headers
    deviced-core
    libddk
    libos-api
    c
)
