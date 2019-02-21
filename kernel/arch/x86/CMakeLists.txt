# Project setup
project (Vali-Platform-X86)
enable_language (ASM_NASM)
enable_language (C)

if (NOT DEFINED VALI_CROSS_TOOLS)
    message (FATAL_ERROR "You must define VALI_CROSS_TOOLS to point to a clang cross compiler")
endif ()

set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set (CMAKE_ASM_NASM_OBJECT_FORMAT bin)
set (CMAKE_ASM_NASM_FLAGS "-D${VALI_PLATFORM} -D__${VALI_PLATFORM}__")
