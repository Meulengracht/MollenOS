# Project setup
project (vali-boot)

# On x86 platforms, enable NASM assembly language support
if (VALI_ARCH STREQUAL "amd64" OR VALI_ARCH STREQUAL "i386")
    enable_language (ASM_NASM)
    string(REPLACE " " ";" NASM_FEATURE_FLAGS "${FEATURE_FLAGS}")
endif ()

# Configure compilation options
if (VALI_BUILD)
    set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${VALI_PATH_DEPLOY})
endif ()

# Configure options
if (NOT VALI_ARCH STREQUAL "amd64" AND NOT VALI_ARCH STREQUAL "i386")
    set(BOOTLOADER_BIOS OFF CACHE BOOL "BIOS Bootloader only available on x86" FORCE)
endif ()

option (BOOTLOADER_BIOS "Enable BIOS bootloader" OFF)
option (BOOTLOADER_UEFI "Enable UEFI bootloader" OFF)
option (BOOTLOADER_RPI  "Enable Raspberry Pi bootloader" OFF)

# If no bootloader option is specified, enable BIOS bootloader by default
if (NOT BOOTLOADER_BIOS AND NOT BOOTLOADER_UEFI AND NOT BOOTLOADER_RPI)
    set(BOOTLOADER_UEFI ON CACHE BOOL "Enable UEFI bootloader" FORCE)
endif ()

# Only compile the x86 trampoline code on amd64 and i386
if (VALI_ARCH STREQUAL "amd64" OR VALI_ARCH STREQUAL "i386")
    add_custom_target(ap ALL DEPENDS "${VALI_PATH_DEPLOY}/ap.sys")
    add_custom_command(OUTPUT "${VALI_PATH_DEPLOY}/ap.sys"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${VALI_PATH_DEPLOY}"
        COMMAND "${CMAKE_ASM_NASM_COMPILER}" -f bin -DTRAMPOLINE_LOCATION=0x8000 -D${VALI_ARCH} -D__${VALI_ARCH}__ ${NASM_FEATURE_FLAGS} -o "${VALI_PATH_DEPLOY}/ap.sys" ap.asm
        VERBATIM
        COMMENT "Compiling trampoline code"
        DEPENDS ap.asm
    )
endif ()

if (BOOTLOADER_BIOS)
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
                -DCMAKE_INSTALL_PREFIX:PATH=${VALI_PATH_BOOT_ASSETS}
                -DVALI_ARCH=${VALI_ARCH}

    )
endif ()

if (BOOTLOADER_RPI)
    add_subdirectory(rpi)
endif ()
