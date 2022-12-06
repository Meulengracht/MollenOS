project (vali-kernel-acpi-hpet)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-acpi-hpet ""
        comparators.c
        hpet.c
)
