
# include sub-directories which the primary project relies on
add_subdirectory(threads)
add_subdirectory(uthreads)
add_subdirectory(services)

# setup primary library
add_library(libos STATIC
        dmabuf.c
        error_codes.c
        futex.c
        memory.c
        sha1.c
        spinlock.c
        syscalls.c
        system.c
        time.c
)
target_include_directories(libos PRIVATE
        ../libc/include
        include
)
target_include_directories(libos PUBLIC
        include
)
target_compile_definitions(libos PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_link_libraries(libos PUBLIC libos-threads libos-uthreads libos-services)

install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
)

# create an interface library for libos as targets are not meant
# to include this library themselves
add_library(libos-api INTERFACE)
target_include_directories(libos-api INTERFACE include)
