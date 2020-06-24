# Project setup
project (Vali-Boot)
enable_language (ASM_NASM)

set (CMAKE_ASM_NASM_FLAGS "-D${VALI_ARCH} -D__${VALI_ARCH}__")
set (CMAKE_ASM_NASM_OBJECT_FORMAT bin)

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

add_custom_target(stage1 ALL DEPENDS stage1.sys)
add_custom_command(OUTPUT stage1.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND nasm -f bin ${CMAKE_ASM_NASM_FLAGS} -o ${VALI_PATH_DEPLOY}/stage1.sys ${SRCS}
    COMMENT "Compiling bootloader (stage1)"
    DEPENDS ${SRCS}
)

add_custom_target(stage2 ALL DEPENDS stage2.sys)
add_custom_command(OUTPUT stage2.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stage2
    COMMAND nasm -f bin ${CMAKE_ASM_NASM_FLAGS} -o ${VALI_PATH_DEPLOY}/stage2.sys stage2.asm
    COMMENT "Compiling bootloader (stage2)"
    DEPENDS stage2/stage2.asm
)

add_custom_target(ap ALL DEPENDS ap.sys)
add_custom_command(OUTPUT ap.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stage2
    COMMAND nasm -f bin -DTRAMPOLINE_LOCATION=0x8000 ${CMAKE_ASM_NASM_FLAGS} -o ${VALI_PATH_DEPLOY}/ap.sys ap.asm
    COMMENT "Compiling trampoline code"
    DEPENDS stage2/ap.asm
)
