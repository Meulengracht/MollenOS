add_library(vafs STATIC
    crc.c
    directory.c
    file.c
    vafs.c
    stream.c
    streamdevice.c
)
target_include_directories(vafs PRIVATE ../include)
