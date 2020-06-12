# Project setup
project (ValiPlatform-amd64)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set (CMAKE_ASM_NASM_OBJECT_FORMAT win32)
set (CMAKE_ASM_NASM_FLAGS "-Xvc -D${VALI_ARCH} -D__${VALI_ARCH}__")

# Configure include paths
include_directories (
        ../../../../librt/include
        ../../../../librt/libc/include
        ../../../../librt/libds/include
        ../../../../librt/libddk/include
        ../../../../librt/libacpi/source/include
        ../../include
        ../../../include
        ../
        ./)

# Add support for .S extension
set_source_files_properties(min.S PROPERTIES LANGUAGE ASM_NASM)

add_library(vali-amd64
        _descriptors.s
        _irq.s
        _thread.s
        boot.s

        gdt.c
        idt.c

        memory/_paging.s
        memory/vmem_api.c
        memory/vmem_boot.c
)
install(TARGETS vali-amd64 LIBRARY DESTINATION lib)
