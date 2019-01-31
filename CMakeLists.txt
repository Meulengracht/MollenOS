cmake_minimum_required (VERSION 3.8)
project (Vali)

# Configure compilation options
set (CMAKE_ASM_COMPILER nasm)

# Configure options
option (X86_MBOOT_FAT32 "Use FAT32 bootloader instead of MFS" OFF) 

# Compile the bootloader for Vali
add_subdirectory (MathFunctions)

# Compile support libraries