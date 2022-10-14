add_library(filed-vfs-handlers
        close.c
        duplicate.c
        flush.c
        handle_stat.c
        link.c
        mkdir.c
        move.c
        open.c
        read.c
        readdir.c
        readlink.c
        realpath.c
        seek.c
        stat.c
        unlink.c
        write.c
)
set_target_properties (filed-vfs-handlers
        PROPERTIES
        COMPILE_FLAGS "-Wno-address-of-packed-member -D__FILEMANAGER_IMPL"
)
target_include_directories(filed-vfs-handlers PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_SOURCE_DIR}/librt/libgracht/include
        ${CMAKE_SOURCE_DIR}/librt/libddk/include
        ${CMAKE_SOURCE_DIR}/librt/libusb/include
        ${CMAKE_SOURCE_DIR}/librt/libds/include
        ${CMAKE_SOURCE_DIR}/librt/libos/include
        ${CMAKE_SOURCE_DIR}/librt/libc/include
)
