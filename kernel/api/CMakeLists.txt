project (vali-kernel-api)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-api ""
    priv.c
    entry.c
    memory.c
    dma.c
    dma_attachment.c
    shm.c
    shm_region.c
    handles.c
    sync.c
    misc.c
    time.c
    threads.c
)
