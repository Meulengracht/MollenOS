project (vali-core-x86-64)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT win64)

# Configure include paths
include_directories (
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

add_kernel_library(vali-amd64 ""
    ${ASM_SOURCES}

    context.c
    gdt.c
    idt.c

    memory/vmem_api.c
    memory/vmem_boot.c
)
