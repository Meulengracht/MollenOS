set (LIBDS_SRCS
        lf/bounded_stack.c

        bitmap.c
        bounded_stack.c
        guid.c
        hashtable.c
        hash_sip.c
        list.c
        queue.c
        rbtree.c
        streambuffer.c
)

# handle unit test support, we need to combine some libDS specific
# support for services and modules that uses data-structures
if (VALI_BUILD_UNIT_TESTS)
    add_subdirectory(mstring)

    add_library(libds ${LIBDS_SRCS} support/ds.c)
    target_include_directories(libds PRIVATE ../libddk/include ../libos/include)
    target_include_directories(libds PUBLIC include)
    target_link_libraries(libds PUBLIC mstring)

    return ()
endif ()


# Add native builds of mstring, and lets request the kernel
# build as well, so we end up with three total builds of mstring
# library. We want one for userspace, kernel and host which can be
# imported into unit tests
add_subdirectory(mstring)

# Configure include paths
set (SHARED_INCLUDES
        ../libddk/include
        ../libos/include
        ../libc/include
        ./include
)
set (KERNEL_INCLUDES
        ../../boot/include
        ../../kernel/include
        ../../kernel/arch/include
)

if (${VALI_ARCH} STREQUAL "i386")
    list (APPEND KERNEL_INCLUDES
        ../../kernel/arch/x86/x32
    )
elseif (${VALI_ARCH} STREQUAL "amd64")
    list (APPEND KERNEL_INCLUDES
        ../../kernel/arch/x86/x64
    )
endif ()

add_library(libdsk ${LIBDS_SRCS} support/dsk.c)
target_compile_options(libdsk PRIVATE -mno-sse)
target_compile_definitions(libdsk PRIVATE -D__LIBDS_KERNEL__ -D_KRNL_DLL)
target_include_directories(libdsk PRIVATE ${SHARED_INCLUDES} ${KERNEL_INCLUDES})
target_include_directories(libdsk PUBLIC include)
target_link_libraries(libdsk PUBLIC mstringk)

add_library(libds ${LIBDS_SRCS} support/ds.c)
target_include_directories(libds PRIVATE ${SHARED_INCLUDES})
target_include_directories(libds PUBLIC include)
target_link_libraries(libds PUBLIC mstring)
install(TARGETS libds mstring
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
