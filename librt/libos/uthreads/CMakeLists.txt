add_library(libos-uthreads STATIC
        cond.c
        job.c
        mutex.c
        once.c
        rwlock.c
        scheduler.c
        xunit.c
)
target_compile_definitions(libos-uthreads PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_link_libraries(libos-uthreads PUBLIC libddk libds libc-api)
target_include_directories(libos-uthreads PRIVATE ../include)
