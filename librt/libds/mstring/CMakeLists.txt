
set(MSTRING_SRCS
        mstr_at.c
        mstr_builder.c
        mstr_case.c
        mstr_clone.c
        mstr_cmp.c
        mstr_conv.c
        mstr_delete.c
        mstr_find.c
        mstr_fmt.c
        mstr_hash.c
        mstr_new.c
        mstr_replace.c
        mstr_substr.c
        mstr_unicode.c
)

# Build the host library if we are unit testing
if (VALI_BUILD_UNIT_TESTS)
    add_library(mstring STATIC ${MSTRING_SRCS})
    target_include_directories(mstring PUBLIC ../include)

    install(TARGETS mstring)
    return ()
endif ()

add_library(mstring STATIC ${MSTRING_SRCS})
target_include_directories(mstring PUBLIC ../include ../../libos/include ../../libc/include)

add_library(mstringk STATIC ${MSTRING_SRCS})
target_compile_options(mstringk PRIVATE -mno-sse)
target_compile_definitions(mstringk PRIVATE -DMSTRING_KERNEL -D_KRNL_DLL)
target_include_directories(mstringk PUBLIC
        ../include
        ${CMAKE_SOURCE_DIR}/kernel/include
        ../../libos/include
        ../../libc/include
)
