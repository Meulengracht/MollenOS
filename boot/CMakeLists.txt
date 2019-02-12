# Project setup
project (Vali-Boot)
enable_language(ASM_NASM)

# Configure compilation options
if (VALI_BUILD)
    set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
endif ()

# Configure options
option (X86_VBOOT_FAT32 "Use FAT32 bootloader instead of MFS" OFF)

# Build the stage 1 bootloader for BIOS
if (X86_VBOOT_FAT32)
    set (SRCS stage1/fat32/stage1.asm)
else ()
    set (SRCS stage1/mfs/stage1.asm)
endif ()
add_library (stage1 ${SRCS})
set_target_properties (stage1 PROPERTIES SUFFIX ".sys")

# Build the stage 2 bootloader for BIOS
set (SRCS stage2/stage2.asm)
add_library (stage2 ${SRCS})
set_target_properties (stage2 PROPERTIES SUFFIX ".sys")

# Build the trampoline code for the cpu
set (SRCS stage2/ap.asm)
add_library (ap ${SRCS})
set_target_properties (ap PROPERTIES SUFFIX ".sys")
