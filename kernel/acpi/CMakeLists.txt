project (vali-kernel-acpi)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-acpi ""
        irq.c
        functions.c
        handlers.c
        hpet.c
        init.c
        osl.c
        scan.c
        early_init.c
)
