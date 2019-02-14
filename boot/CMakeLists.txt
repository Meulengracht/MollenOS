# Project setup
project (Vali-Boot)
enable_language (ASM_NASM)

set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set (CMAKE_ASM_NASM_OBJECT_FORMAT bin)
set (CMAKE_ASM_NASM_FLAGS "-D${VALI_PLATFORM} -D__${VALI_PLATFORM}__")

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
set_property (TARGET stage1 PROPERTY SUFFIX ".sys")

# Build the stage 2 bootloader for BIOS
add_library (stage2 stage2/stage2.asm)
set_property (TARGET stage2 PROPERTY SUFFIX ".sys")

# Build the trampoline code for the cpu
add_library (ap stage2/ap.asm)
set_property (TARGET ap PROPERTY SUFFIX ".sys")
