# include sub-directories which the primary project relies on
add_subdirectory(threads)
add_subdirectory(uthreads)
add_subdirectory(services)

# setup primary library
add_library(libos STATIC
        error_codes.c
        event.c
        futex.c
        handles.c
        handles_setup.c
        ipc.c
        memory.c
        notification_queue.c
        sha1.c
        shm.c
        spinlock.c
        syscalls.c
        system.c
        time.c
        unwind.c
)
target_include_directories(libos PUBLIC
        include
)
target_compile_definitions(libos PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_link_libraries(libos PUBLIC libos-threads libos-uthreads libos-services libc-api)

install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
)

# create an interface library for libos as targets are not meant
# to include this library themselves
add_library(libos-api INTERFACE)
target_include_directories(libos-api INTERFACE include)
