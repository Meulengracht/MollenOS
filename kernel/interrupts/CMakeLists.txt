project (vali-kernel-interrupts)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-interrupts ""
    chain.c
    group.c
    handlers.c
    interrupts.c
    table.c
)
