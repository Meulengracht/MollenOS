# Project setup
project (vali-boot-bios)
enable_language (ASM_NASM)

string(REPLACE " " ";" NASM_FEATURE_FLAGS "${FEATURE_FLAGS}")

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

add_custom_target(mbr ALL DEPENDS "${VALI_PATH_DEPLOY}/mbr.sys")
add_custom_command(OUTPUT "${VALI_PATH_DEPLOY}/mbr.sys"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VALI_PATH_DEPLOY}"
    COMMAND "${CMAKE_ASM_NASM_COMPILER}" -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o "${VALI_PATH_DEPLOY}/mbr.sys" mbr.asm
    VERBATIM
    COMMENT "Compiling bootloader (mbr)"
    DEPENDS mbr.asm
)

add_custom_target(mbr-gpt ALL DEPENDS "${VALI_PATH_DEPLOY}/mbr-gpt.sys")
add_custom_command(OUTPUT "${VALI_PATH_DEPLOY}/mbr-gpt.sys"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VALI_PATH_DEPLOY}"
    COMMAND "${CMAKE_ASM_NASM_COMPILER}" -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o "${VALI_PATH_DEPLOY}/mbr-gpt.sys" mbr-gpt.asm
    VERBATIM
    COMMENT "Compiling bootloader (mbr-gpt)"
    DEPENDS mbr-gpt.asm
)

add_custom_target(stage1 ALL DEPENDS "${VALI_PATH_DEPLOY}/stage1.sys")
add_custom_command(OUTPUT "${VALI_PATH_DEPLOY}/stage1.sys"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VALI_PATH_DEPLOY}"
    COMMAND "${CMAKE_ASM_NASM_COMPILER}" -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o "${VALI_PATH_DEPLOY}/stage1.sys" ${SRCS}
    VERBATIM
    COMMENT "Compiling bootloader (stage1)"
    DEPENDS ${SRCS}
)

add_custom_target(stage2 ALL DEPENDS "${VALI_PATH_DEPLOY}/stage2.sys")
add_custom_command(OUTPUT "${VALI_PATH_DEPLOY}/stage2.sys"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/stage2"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VALI_PATH_DEPLOY}"
    COMMAND "${CMAKE_ASM_NASM_COMPILER}" -f bin -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o "${VALI_PATH_DEPLOY}/stage2.sys" stage2.asm
    VERBATIM
    COMMENT "Compiling bootloader (stage2)"
    DEPENDS stage2/stage2.asm
)

# setup bootloader output
install(
    FILES 
        "${VALI_PATH_DEPLOY}/mbr.sys"
        "${VALI_PATH_DEPLOY}/mbr-gpt.sys"
        "${VALI_PATH_DEPLOY}/stage1.sys"
        "${VALI_PATH_DEPLOY}/stage2.sys"
    DESTINATION boot/loaders
)
