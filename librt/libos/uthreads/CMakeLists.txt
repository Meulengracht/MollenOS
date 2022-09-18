
add_library(libos-uthreads STATIC
        cond.c
        job.c
        mutex.c
        once.c
        scheduler.c
        xunit.c
)
target_compile_definitions(libos-uthreads PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_include_directories(libos-uthreads PRIVATE
        ../../libc/include
        ../../libddk/include
        ../../libds/include
        ../include
)
