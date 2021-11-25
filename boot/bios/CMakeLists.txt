# Project setup
project (vali-boot-bios)
enable_language (ASM_NASM)

string(REPLACE " " ";" NASM_FEATURE_FLAGS ${FEATURE_FLAGS})

# Configure compilation options
if (VALI_BUILD)
    set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
endif ()

# Configure options
option (X86_BOOTLOADER_BIOS_FAT "Use FAT32 boot partition instead of MFS" OFF)

# Build the stage 1 bootloader for BIOS
if (X86_BOOTLOADER_BIOS_FAT)
    set (SRCS stage1/fat32/stage1.asm)
else ()
    set (SRCS stage1/mfs/stage1.asm)
endif ()

add_custom_target(mbr ALL DEPENDS mbr.sys)
add_custom_command(OUTPUT mbr.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND nasm ARGS -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o ${VALI_PATH_DEPLOY}/mbr.sys mbr.asm
    COMMENT "Compiling bootloader (mbr)"
    DEPENDS mbr.asm
)

add_custom_target(mbr-gpt ALL DEPENDS mbr-gpt.sys)
add_custom_command(OUTPUT mbr-gpt.sys POST_BUILD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND nasm ARGS -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o ${VALI_PATH_DEPLOY}/mbr-gpt.sys mbr-gpt.asm
        COMMENT "Compiling bootloader (mbr-gpt)"
        DEPENDS mbr-gpt.asm
)

add_custom_target(stage1 ALL DEPENDS stage1.sys)
add_custom_command(OUTPUT stage1.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND nasm ARGS -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o ${VALI_PATH_DEPLOY}/stage1.sys ${SRCS}
    COMMENT "Compiling bootloader (stage1)"
    DEPENDS ${SRCS}
)

add_custom_target(stage2 ALL DEPENDS stage2.sys)
add_custom_command(OUTPUT stage2.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stage2
    COMMAND nasm ARGS -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o ${VALI_PATH_DEPLOY}/stage2.sys stage2.asm
    COMMENT "Compiling bootloader (stage2)"
    DEPENDS stage2/stage2.asm
)

