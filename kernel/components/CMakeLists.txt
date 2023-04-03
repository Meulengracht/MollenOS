project (vali-kernel-components)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-components ""
        cpu.c
        domain.c
        ic.c
        memory.c
        scheduler.c
        timer.c
)
