project (vali-kernel-utils)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-utils ""
        crc32.c
        dynamic_memory_pool.c
        static_memory_pool.c
)
