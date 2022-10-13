set(SRCS
        strcasecmp.c
        strcat.c
        strchr.c
        strcmp.c
        strcoll.c
        strcpy.c
        strcspn.c
        strdup.c
        strlen.c
        strncasecmp.c
        strncat.c
        strncmp.c
        strncpy.c
        strnlen.c
        strpbrk.c
        strrchr.c
        strspn.c
        strstr.c
        strtok.c
        strxfrm.c
)
add_libk_target(libk-str ${SRCS})
add_libc_target(libc-str ${SRCS})
