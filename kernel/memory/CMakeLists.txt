if (__BUILD_UNIT_TESTS)
    set (KMEM_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/librt/libds/include
        ${CMAKE_SOURCE_DIR}/librt/libddk/include
        ${CMAKE_SOURCE_DIR}/librt/libos/include
        ${CMAKE_SOURCE_DIR}/boot/include
    )

    add_subdirectory(space)
    add_subdirectory(shm)
    return ()
endif ()

enable_language (C)

add_kernel_library (vali-core-memory ""
    # Kernel heap and physical memory initialization.
    heap.c
    memory.c
    memory_region.c

    # Virtual address spaces and tracked mappings.
    space/allocations.c
    space/context.c
    space/create.c
    space/map.c
    space/mapping.c
    space/struct.c
    space/sync.c
    space/unmap.c
    space/usercopy.c
    space/utils.c

    # Physical-to-device address conversion and mapping cleanup.
    dma/address.c
    dma/direct.c

    # Device-address reservations for translated DMA mappings.
    dma/iova.c

    # Privileged DMA manager lifetime and attachment administration.
    dma/manager.c
    dma/binding.c
    dma/groups.c
    dma/recovery.c
    dma/events.c

    # Shared buffers, restricted views and device access.
    shm/shm.c
    shm/view.c
    shm/access.c
    shm/region.c
    shm/device/device.c
    shm/device/cleanup.c
    shm/device/attachment.c
    shm/device/context.c
)
