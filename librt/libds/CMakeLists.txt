# Project setup
project (ValiDS)
enable_language (C)

# Configure include paths
set (SHARED_INCLUDES
    ../libddk/include
    ../libc/include
    ../include
    ./include
)
set (KERNEL_INCLUDES "")

if (${VALI_ARCH} STREQUAL "i386")
    list (APPEND KERNEL_INCLUDES
        ../../kernel/include
        ../../kernel/arch/include
        ../../kernel/arch/x86/x32
    )
elseif (${VALI_ARCH} STREQUAL "amd64")
    list (APPEND KERNEL_INCLUDES
        ../../kernel/include
        ../../kernel/arch/include
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

    pe/load.c
    pe/utilities.c
    pe/verify.c

    bitmap.c
    bounded_stack.c
    collection.c
    hashtable.c
    list.c
    queue.c
    rbtree.c
    streambuffer.c
)

add_library(libdsk ${SHARED_SOURCES} support/dsk.c)
set_target_properties(libdsk PROPERTIES COMPILE_FLAGS -mno-sse -D__LIBDS_KERNEL__ -D_KRNL_DLL)
target_include_directories(libdsk PUBLIC ${SHARED_INCLUDES} ${KERNEL_INCLUDES})

add_library(libds ${SHARED_SOURCES} support/ds.c)
target_include_directories(libds PUBLIC ${SHARED_INCLUDES})
install(TARGETS libds
    ARCHIVE DESTINATION lib/vali-sdk
    LIBRARY DESTINATION lib/vali-sdk
    RUNTIME DESTINATION bin/vali-sdk
)
install(DIRECTORY include/ DESTINATION include/vali-sdk)
