add_library(vafs STATIC
    crc.c
    directory.c
    file.c
    log.c
    vafs.c
    stream.c
    streamdevice.c
)
target_include_directories(vafs PRIVATE include/vafs)
target_include_directories(vafs PUBLIC include)
