
set(MSTRING_SRCS
        common/mstr_builder.c

        core/mstr_at.c
        core/mstr_case.c
        core/mstr_clone.c
        core/mstr_cmp.c
        core/mstr_conv.c
        core/mstr_delete.c
        core/mstr_find.c
        core/mstr_fmt.c
        core/mstr_hash.c
        core/mstr_join.c
        core/mstr_new.c
        core/mstr_replace.c
        core/mstr_split.c
        core/mstr_substr.c
        core/mstr_unicode.c

        path/mstr_path_basename.c
        path/mstr_path_change_extension.c
        path/mstr_path_dirname.c
        path/mstr_path_join.c
        path/mstr_path_new_u8.c
        path/mstr_path_tokens.c
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
target_compile_definitions(mstringk PRIVATE -DMSTRING_KERNEL)
target_include_directories(mstringk PUBLIC
        ../include
        ${CMAKE_SOURCE_DIR}/kernel/include
        ../../libos/include
        ../../libc/include
)
