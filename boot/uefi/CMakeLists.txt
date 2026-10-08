# CMake configuration for building the UEFI bootloader using edk2
cmake_minimum_required(VERSION 3.14)

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

# Fetch the official edk2 repository, but we stick it to the
# stable tag releases
FetchContent_Declare(
    edk2
    GIT_REPOSITORY https://github.com/tianocore/edk2.git
    GIT_TAG        edk2-stable202608
    GIT_SHALLOW    1
)

find_program(MAKE_EXECUTABLE make REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if (NOT CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER clang REQUIRED)
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
set(EDK_BUILD_WRAPPERS "${EDK_TOOLS_PATH}/BinWrappers/PosixLike")
set(EDK_BUILD_ROOT "${CMAKE_CURRENT_BINARY_DIR}/Build/VBoot")
set(EDK_OUTPUT "${EDK_BUILD_ROOT}/RELEASE_CLANGPDB/${EDK_ARCH}/VBoot.efi")
set(BOOT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${BOOT_FILENAME}")

file(MAKE_DIRECTORY "${EDK_CONF_PATH}")
configure_file("${EDK_TOOLS_PATH}/Conf/target.template" "${EDK_CONF_PATH}/target.txt" COPYONLY)
configure_file("${EDK_TOOLS_PATH}/Conf/tools_def.template" "${EDK_CONF_PATH}/tools_def.txt" COPYONLY)
configure_file("${EDK_TOOLS_PATH}/Conf/build_rule.template" "${EDK_CONF_PATH}/build_rule.txt" COPYONLY)

set(EDK_ENV
    "WORKSPACE=${EDK_WORKSPACE}"
    "PACKAGES_PATH=${EDK_WORKSPACE}:${CMAKE_CURRENT_SOURCE_DIR}"
    "CONF_PATH=${EDK_CONF_PATH}"
    "EDK_TOOLS_PATH=${EDK_TOOLS_PATH}"
    "PYTHON_COMMAND=${Python3_EXECUTABLE}"
    "CLANG_BIN=${CLANG_BIN}/"
    "CLANG_HOST_BIN=${CLANG_HOST_BIN}/"
    "PATH=${EDK_BUILD_WRAPPERS}:$ENV{PATH}"
)

add_custom_target(edk2-basetools
    COMMAND "${MAKE_EXECUTABLE}" -C "${EDK_TOOLS_PATH}/Source/C"
    USES_TERMINAL
)

add_custom_target(vboot ALL
    COMMAND ${CMAKE_COMMAND} -E env ${EDK_ENV}
        "${EDK_BUILD_WRAPPERS}/build"
        -p "${CMAKE_CURRENT_SOURCE_DIR}/VBoot.dsc"
        -a "${EDK_ARCH}"
        -t CLANGPDB
        -b RELEASE
        -D "VBOOT_OUTPUT_DIRECTORY=${EDK_BUILD_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EDK_OUTPUT}" "${BOOT_OUTPUT}"
    DEPENDS edk2-basetools
    BYPRODUCTS "${BOOT_OUTPUT}"
    USES_TERMINAL
)

install(FILES "${BOOT_OUTPUT}" DESTINATION boot/EFI/BOOT)
