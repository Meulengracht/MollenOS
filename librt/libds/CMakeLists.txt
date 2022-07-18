# Project setup
project (ValiDS)
enable_language (C)

add_subdirectory(mstring2)

# Configure include paths
set (SHARED_INCLUDES
    ../libddk/include
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

set (SHARED_SOURCES
    lf/bounded_stack.c
    
    mstring/mstringappend.c
    mstring/mstringcompare.c
    mstring/mstringcopy.c
    mstring/mstringcreate.c
    mstring/mstringdestroy.c
    mstring/mstringfind.c
    mstring/mstringhash.c
    mstring/mstringiterate.c
    mstring/mstringlength.c
    mstring/mstringmisc.c
    mstring/mstringprint.c
    mstring/mstringraw.c
    mstring/mstringresize.c
    mstring/mstringsubstring.c
    mstring/mstringutf8.c

    bitmap.c
    bounded_stack.c
    collection.c
    guid.c
    hashtable.c
    hash_sip.c
    list.c
    queue.c
    rbtree.c
    streambuffer.c
)

add_library(libdsk ${SHARED_SOURCES} support/dsk.c)
target_compile_options(libdsk PRIVATE -mno-sse)
target_compile_definitions(libdsk PRIVATE -D__LIBDS_KERNEL__ -D_KRNL_DLL)
target_include_directories(libdsk PRIVATE ${SHARED_INCLUDES} ${KERNEL_INCLUDES})
target_include_directories(libdsk PUBLIC include)

add_library(libds ${SHARED_SOURCES} support/ds.c)
target_include_directories(libds PRIVATE ${SHARED_INCLUDES})
target_include_directories(libds PUBLIC include)
install(TARGETS libds
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
