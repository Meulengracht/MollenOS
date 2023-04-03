project (vali-kernel-threads)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-threads ""
        threads.c
)
