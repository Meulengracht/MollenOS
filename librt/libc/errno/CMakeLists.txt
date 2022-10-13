set(SRCS
        errno.c
        errstrings.c
)
add_libk_target(libk-errno ${SRCS})
add_libc_target(libc-errno ${SRCS})
