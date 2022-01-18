cmake_minimum_required(VERSION 3.14.3)
project(vafs C)

add_executable(mkvafs
    main.c
    vafs/crc.c
    vafs/directory.c
    vafs/vafs.c
    vafs/stream.c
)
target_include_directories(mkvafs PRIVATE include)
