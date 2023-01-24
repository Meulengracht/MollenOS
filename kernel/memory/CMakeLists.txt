project (vali-kernel-memory)
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
