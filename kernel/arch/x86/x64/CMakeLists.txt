# Project setup
project (ValiPlatform-amd64)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT win64)

# Configure include paths
include_directories (
        ../../../../boot/include
        ../../../../librt/libc/include
        ../../../../librt/libds/include
        ../../../../librt/libddk/include
        ../../../../librt/libacpi/source/include
        ../../include
        ../../../include
        ../
        ./
)

set (ASM_SOURCES
    memory/_paging.s
    _descriptors.s
    _irq.s
    _thread.s
    boot.s
    portio.s
)

# Add support for .S extension
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

add_library(vali-amd64 STATIC
    ${ASM_SOURCES}

    context.c
    gdt.c
    idt.c

    memory/vmem_api.c
    memory/vmem_boot.c
)
target_compile_options(vali-amd64
    PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse -DUNICODE -DLIBC_KERNEL -D_KRNL_DLL -D__LIBDS_KERNEL__>
)
