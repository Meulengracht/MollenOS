project (vali-kernel-acpi)
enable_language (C)

# Add any sub-projects
add_subdirectory(hpet)

# Configure source files
add_kernel_library (vali-core-acpi ""
        irq.c
        functions.c
        handlers.c
        init.c
        osl.c
        scan.c
        early_init.c
)
