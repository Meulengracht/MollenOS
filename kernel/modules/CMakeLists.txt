project (vali-kernel-modules)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-modules ""
        manager.c
        module.c
        ramdisk.c
)
