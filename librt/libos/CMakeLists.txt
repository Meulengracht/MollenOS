
# include sub-directories which the primary project relies on
add_subdirectory(threads)
add_subdirectory(uthreads)

# setup primary library
add_library(libos STATIC
        dmabuf.c
        error_codes.c
        futex.c
        memory.c
        sha1.c
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
target_link_libraries(libos PUBLIC libos-threads libos-uthreads)

install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
)
