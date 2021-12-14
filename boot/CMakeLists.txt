# Project setup
project (vali-boot)
enable_language (ASM_NASM)

string(REPLACE " " ";" NASM_FEATURE_FLAGS ${FEATURE_FLAGS})

# Configure compilation options
if (VALI_BUILD)
    set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
endif ()

# Configure options
option (X86_BOOTLOADER_BIOS "Enable BIOS bootloader" ON)
option (BOOTLOADER_UEFI "Enable UEFI bootloader" ON)

add_custom_target(ap ALL DEPENDS ap.sys)
add_custom_command(OUTPUT ap.sys POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND nasm ARGS -f bin -DTRAMPOLINE_LOCATION=0x8000 -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o ${VALI_PATH_DEPLOY}/ap.sys ap.asm
    COMMENT "Compiling trampoline code"
    DEPENDS ap.asm
)

if (X86_BOOTLOADER_BIOS)
    add_subdirectory(bios)
endif ()

if (BOOTLOADER_UEFI)
    # We unfortunately need to provide a different compiler environment
    # so we need to include this as an external project
    include(ExternalProject)
    ExternalProject_Add(uefi
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/uefi
            CMAKE_ARGS
                -DCMAKE_C_COMPILER:PATH=${CMAKE_C_COMPILER}
                -DCMAKE_C_COMPILER_FORCED=ON
                -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}
                -DVALI_ARCH=${VALI_ARCH}

    )
endif ()
