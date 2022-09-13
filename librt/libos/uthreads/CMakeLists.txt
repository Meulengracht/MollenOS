
add_library(libos-uthreads STATIC
        cond.c
        mutex.c
        scheduler.c
        xunit.c
)
target_include_directories(libos-uthreads PRIVATE
        ../../libc/include
        ../../libddk/include
        ../include
)
