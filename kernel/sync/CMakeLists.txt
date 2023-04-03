project (vali-kernel-sync)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-sync ""
        futex.c
        spinlock.c
        mutex.c
        semaphore.c
        signal.c
        userevent.c
)
