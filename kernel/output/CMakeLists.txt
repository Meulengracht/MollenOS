project (vali-kernel-output)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-output ""
        fonts/font6x12.c
        fonts/font8x16.c
        console.c
        log.c
)
