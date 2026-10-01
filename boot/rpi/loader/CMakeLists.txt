cmake_minimum_required(VERSION 3.20)

# We can use the default aarch64 target here as we dont need the
# vali toolchain. We are building a standalone ELF loader for 
# the RPi loader.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(vali-rpi-loader C ASM)

# Read the VALI_PLATFORM_VARIANT to get the board version
if(NOT VALI_PLATFORM_VARIANT STREQUAL "4" AND NOT VALI_PLATFORM_VARIANT STREQUAL "5")
    message(FATAL_ERROR "VALI_PLATFORM_VARIANT must be 4 or 5")
endif()

# Translate it into the code variable used in the assembly source files
set(RPI_BOARD "")
if(VALI_PLATFORM_VARIANT STREQUAL "4")
    set(RPI_BOARD "4")
elseif(VALI_PLATFORM_VARIANT STREQUAL "5")
    set(RPI_BOARD "5")
endif()

find_program(RPI_LD NAMES ld.lld REQUIRED)

get_filename_component(VALI_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# Compile the shared source again for ELF; kernel-built PE objects and full.c
# cannot participate in a freestanding firmware entry executable.
add_executable(rpi-loader 
    boot_${RPI_BOARD}.S 
    entry.S 
    transfer.S 
    init.c 
    header.c 
    pe.c 
    contract.c 
    platform.c 
    runtime.c 
    devicetree.c
    "${VALI_SOURCE_ROOT}/kernel/devicetree/parser.c"
)

set_target_properties(rpi-loader PROPERTIES SUFFIX ".elf")
target_include_directories(rpi-loader PRIVATE
    "${VALI_SOURCE_ROOT}/boot/include"
    "${VALI_SOURCE_ROOT}/librt/libos/include"
    "${VALI_SOURCE_ROOT}/kernel/include"
    "${VALI_SOURCE_ROOT}/kernel/devicetree"
    "${VALI_SOURCE_ROOT}/librt/libc/include"
)

target_compile_options(
    rpi-loader PRIVATE
    -ffreestanding -fms-extensions -fno-builtin -fno-stack-protector -fno-pic
    -mgeneral-regs-only -march=armv8-a -mno-outline-atomics -ffixed-x18
    "$<$<COMPILE_LANGUAGE:C>:-std=c11;-Wall;-Wextra;-Werror;-mstrict-align;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-O2>"
)

target_link_options(
    rpi-loader PRIVATE -nostdlib "-fuse-ld=${RPI_LD}"
    "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/loader.ld" -Wl,--build-id=none
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/rpi-loader.map"
)

set_property(TARGET rpi-loader APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/loader.ld")

# install the loader where requested under /bin
install(TARGETS rpi-loader DESTINATION bin)
