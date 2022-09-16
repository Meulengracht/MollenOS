
add_library(libos-uthreads STATIC
        cond.c
        job.c
        mutex.c
        once.c
        scheduler.c
        xunit.c
)
target_include_directories(libos-uthreads PRIVATE
        ../../libc/include
        ../../libddk/include
        ../../libds/include
        ../include
)
