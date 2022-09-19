
add_library(libos-threads STATIC
        binarysemaphore.c
        condition.c
        mutex.c
        once.c
        thread.c
)
target_compile_definitions(libos-threads PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_include_directories(libos-threads PRIVATE
        ../../libc/include
        ../include
)
