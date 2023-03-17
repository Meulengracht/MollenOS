project (vali-kernel-api)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-api ""
        priv.c
        entry.c
        memory.c
        handles.c
        sync.c
        misc.c
        time.c
        threads.c
)
