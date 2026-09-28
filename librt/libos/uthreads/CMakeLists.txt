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

if (VALI_ARCH STREQUAL "aarch64" OR VALI_ARCH STREQUAL "arm64")
    set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
    enable_language(ASM)
    target_sources(libos-uthreads PRIVATE aarch64/context.S)
endif ()
