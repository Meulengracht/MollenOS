project (vali-kernel-api)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-api ""
        driver_api.c
        entry.c
        memory_api.c
        ossupport_api.c
        synchonization_api.c
        system_api.c
        time_api.c
        thread_api.c
)
