project (vali-kernel-scheduling)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-scheduling ""
        futex.c
        ipc.c
        spinlock.c
        mutex.c
        scheduler.c
        semaphore.c
        signal.c
        threading.c
        userevent.c
)
