add_library(virtio-common STATIC
    pci.c
    queue.c
)
target_link_libraries(virtio-common PUBLIC libddk c librt)
target_include_directories(virtio-common PUBLIC
    include
    ../../../librt/libos/include
)