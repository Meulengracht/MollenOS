project (vali-kernel-memory)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-memory ""
        heap.c
        memory.c
        memory_region.c
        memory_space.c
)
