project (vali-kernel-scheduling)
enable_language (C)

# Configure source files
add_kernel_library (vali-core-scheduling ""
        futex.c
        ipc_context.c
        irq_spinlock.c
        mutex.c
        scheduler.c
        semaphore.c
        signal.c
        threading.c
        userevent.c
)
