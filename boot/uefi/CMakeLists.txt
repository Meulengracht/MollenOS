# CMake configuration for building the UEFI bootloader using edk2
cmake_minimum_required(VERSION 3.17)

# The project is built as an external project, so declare this
# as a separate project
project(vali-boot-uefi NONE)

# We need FetchContent to get the edk2 repository
include(FetchContent)

# Decide on naming and EDK_ARCH
if (VALI_ARCH STREQUAL "amd64")
    set(EDK_ARCH X64)
    set(BOOT_FILENAME BOOTX64.EFI)
elseif (VALI_ARCH STREQUAL "i386")
    set(EDK_ARCH IA32)
    set(BOOT_FILENAME BOOTIA32.EFI)
elseif (VALI_ARCH STREQUAL "aarch64")
    set(EDK_ARCH AARCH64)
    set(BOOT_FILENAME BOOTAA64.EFI)
else ()
    message(FATAL_ERROR "Unsupported UEFI architecture: ${VALI_ARCH}")
endif ()

# Keep EDK2 shallow and pinned to a stable release. Only fetch the submodules
# needed by our build: Brotli for BaseTools, plus the Brotli and MIPI Sys-T
# include paths declared by MdeModulePkg.dec and MdePkg.dec. EDK2 validates
# these paths even when VBoot does not use the corresponding libraries.
# Recheck this list when updating EDK2 or adding firmware libraries.
FetchContent_Declare(
    edk2
    GIT_REPOSITORY https://github.com/tianocore/edk2.git
    GIT_TAG        edk2-stable202608
    GIT_SHALLOW    1
    GIT_PROGRESS   TRUE
    GIT_SUBMODULES
        BaseTools/Source/C/BrotliCompress/brotli
        MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
        MdePkg/Library/MipiSysTLib/mipisyst
    GIT_SUBMODULES_RECURSE FALSE
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if (NOT CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER clang REQUIRED)
endif ()

# BaseTools are host programs; the firmware is always built with CLANGPDB.
if (CMAKE_HOST_WIN32)
    find_program(MAKE_EXECUTABLE nmake)
    find_program(EDK_HOST_C_COMPILER cl)
    if (NOT MAKE_EXECUTABLE OR NOT EDK_HOST_C_COMPILER)
        message(FATAL_ERROR "Build UEFI from a Visual Studio Developer PowerShell (C++ tools required).")
    endif ()

    # Match the active MSVC environment, independently of the firmware target.
    if ("$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "x64")
        set(EDK_HOST_ARCH X64)
        set(EDK_HOST_BIN Win64)
    elseif ("$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "x86")
        set(EDK_HOST_ARCH IA32)
        set(EDK_HOST_BIN Win32)
    elseif ("$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "arm64")
        set(EDK_HOST_ARCH AARCH64)
        set(EDK_HOST_BIN Win64)
    else ()
        message(FATAL_ERROR "VSCMD_ARG_TGT_ARCH is unset or unsupported. Use a Visual Studio Developer PowerShell.")
    endif ()
else ()
    find_program(MAKE_EXECUTABLE make REQUIRED)
endif ()

if (EDK_ARCH STREQUAL "X64" OR EDK_ARCH STREQUAL "IA32")
    find_program(EDK_NASM_EXECUTABLE nasm REQUIRED)
    get_filename_component(EDK_NASM_BIN "${EDK_NASM_EXECUTABLE}" DIRECTORY)
endif ()

# After the checks, make it available
message(STATUS "Fetching and configuring edk2, this might take a while")
FetchContent_MakeAvailable(edk2)

get_filename_component(CLANG_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(CLANG_HOST_BIN "${MAKE_EXECUTABLE}" DIRECTORY)

# Setup paths for the EDK2 build environment
set(EDK_WORKSPACE "${edk2_SOURCE_DIR}")
set(EDK_TOOLS_PATH "${EDK_WORKSPACE}/BaseTools")
set(EDK_CONF_PATH "${CMAKE_CURRENT_BINARY_DIR}/Conf")
set(EDK_BUILD_ROOT "${CMAKE_CURRENT_BINARY_DIR}/Build/VBoot")
set(EDK_OUTPUT "${EDK_BUILD_ROOT}/RELEASE_CLANGPDB/${EDK_ARCH}/VBoot.efi")
set(BOOT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${BOOT_FILENAME}")

file(MAKE_DIRECTORY "${EDK_CONF_PATH}")
configure_file("${EDK_TOOLS_PATH}/Conf/target.template" "${EDK_CONF_PATH}/target.txt" COPYONLY)
configure_file("${EDK_TOOLS_PATH}/Conf/tools_def.template" "${EDK_CONF_PATH}/tools_def.txt" COPYONLY)
configure_file("${EDK_TOOLS_PATH}/Conf/build_rule.template" "${EDK_CONF_PATH}/build_rule.txt" COPYONLY)

# EDK II selects its makefile syntax from MAKE_PATH. Keep Clang's compile and
# link rules, but use native NMake rules on Windows. Modify only our Conf copy.
if (CMAKE_HOST_WIN32)
    file(TO_NATIVE_PATH "${MAKE_EXECUTABLE}" EDK_NMAKE)
    file(APPEND "${EDK_CONF_PATH}/tools_def.txt"
        "\n# Native Windows host for the CLANGPDB toolchain.\n"
        "*_CLANGPDB_*_MAKE_PATH = ${EDK_NMAKE}\n"
        "*_CLANGPDB_*_MAKE_FLAGS = /nologo\n")
endif ()

# A script keeps Windows' semicolon-separated environment values out of CMake
# command lists, and inherits the developer environment at build time.
configure_file(BuildEdk2.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/BuildEdk2.cmake" @ONLY)

add_custom_target(edk2-basetools
    COMMAND "${CMAKE_COMMAND}" -DEDK_BUILD_STEP=basetools -P "${CMAKE_CURRENT_BINARY_DIR}/BuildEdk2.cmake"
    VERBATIM
    USES_TERMINAL
)

add_custom_target(vboot ALL
    COMMAND "${CMAKE_COMMAND}" -DEDK_BUILD_STEP=firmware -P "${CMAKE_CURRENT_BINARY_DIR}/BuildEdk2.cmake"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${EDK_OUTPUT}" "${BOOT_OUTPUT}"
    DEPENDS edk2-basetools
    BYPRODUCTS "${BOOT_OUTPUT}"
    VERBATIM
    USES_TERMINAL
)

install(FILES "${BOOT_OUTPUT}" DESTINATION boot/EFI/BOOT)
