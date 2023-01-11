project (vali-kernel-memory)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-memory ""
        heap.c
        memory.c
        shm.c
        memoryspace.c
)
