
set(MSTRING_SRCS
        mstr_at.c
        mstr_case.c
        mstr_clone.c
        mstr_cmp.c
        mstr_conv.c
        mstr_delete.c
        mstr_find.c
        mstr_hash.c
        mstr_new.c
        mstr_replace.c
        mstr_substr.c
        mstr_unicode.c
)

add_library(mstring STATIC ${MSTRING_SRCS})
target_include_directories(mstring PUBLIC ../include ../../libc/include)
