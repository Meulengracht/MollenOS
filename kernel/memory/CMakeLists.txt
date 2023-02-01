if (__BUILD_UNIT_TESTS)
    set (KMEM_INCLUDES
            ../include
            ${CMAKE_SOURCE_DIR}/librt/libds/include
            ${CMAKE_SOURCE_DIR}/librt/libos/include
            ${CMAKE_SOURCE_DIR}/boot/include
    )

    add_unit_test(FILE ms_allocations_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
    add_unit_test(FILE ms_context_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
    add_unit_test(FILE ms_map_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
    return ()
endif ()

enable_language (C)

# Configure source files
add_kernel_library (vali-core-memory ""
        heap.c
        memory.c
        ms_allocations.c
        ms_context.c
        ms_create.c
        ms_map.c
        ms_shm.c
        ms_struct.c
        ms_sync.c
        ms_unmap.c
        ms_utils.c
)
