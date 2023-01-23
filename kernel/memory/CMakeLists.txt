project (vali-kernel-memory)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-memory ""
        heap.c
        memory.c
        memoryspace.c
        ms_allocations.c
        ms_context.c
        ms_sync.c
        ms_shm.c
)
