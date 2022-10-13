set(SRCS
        memchr.c
        memcmp.c
        memcpy.c
        memmove.c
        memset.c
)
add_libk_target(libk-mem ${SRCS})
add_libc_target(libc-mem ${SRCS})
