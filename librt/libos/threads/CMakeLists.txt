
add_library(libos-threads STATIC
        binarysemaphore.c
        condition.c
        mutex.c
        once.c
        thread.c
)
target_include_directories(libos-threads PRIVATE
        ../../libc/include
        ../include
)
