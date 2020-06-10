# Project setup
project (Vali-Platform-X86-32)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set (CMAKE_ASM_NASM_OBJECT_FORMAT win32)
set (CMAKE_ASM_NASM_FLAGS "-Xvc -D${VALI_PLATFORM} -D__${VALI_PLATFORM}__")

# Configure include paths
include_directories (
        ../../../../librt/include
        ../../../../librt/libc/include
        ../../../../librt/libds/include
        ../../../../librt/libddk/include
        ../../../../librt/libacpi/include
        ../../../include)

# Add support for .S extension
set_source_files_properties(min.S PROPERTIES LANGUAGE ASM_NASM)

add_library(vali-i386
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
