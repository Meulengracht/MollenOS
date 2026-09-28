# Configuration file for the amd64 platform, specifies compile options
# and the projects needed for this target
if (NOT DEFINED VALI_BUILD)
    message (FATAL_ERROR "You must invoke the root cmake file, not the individual platform files")
endif ()

set (ARCH_FLAGS "--target=aarch64-uml-vali")
set (WARNINGS_FLAGS "-Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function")
# Keep Clang builtin headers (stdint.h, stdarg.h, etc.); use explicit OS library headers.
set (SHARED_FLAGS "-fms-extensions -Wall -ffreestanding -nostdlib -nostdlibinc -O3") # -flto

set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_MMIO")
set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_ACPI_SUPPORT")
set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_UART")
set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_DWCAS") # Assume presence of CPUID_FEAT_ECX_CX16 in cpuid
if (NOT VALI_HEADLESS)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_VIDEO")
endif ()

set (ASM_FLAGS "--target=aarch64-uml-vali")
set (VALI_TARGET_TRIPPLE "aarch64-uml-vali")

# We don't use NASM for aarch64 like on x86 platforms, so we use
# clangs in-built assembler will be used for aarch64 assembly files.
set (CMAKE_ASM_FLAGS "${ASM_FLAGS} ${CMAKE_ASM_FLAGS}")

set (CMAKE_C_FLAGS "${ARCH_FLAGS} ${SHARED_FLAGS} ${WARNINGS_FLAGS} ${FEATURE_FLAGS}")
set (CMAKE_CXX_FLAGS "${ARCH_FLAGS} -std=c++17 ${SHARED_FLAGS} ${WARNINGS_FLAGS} ${FEATURE_FLAGS}")

set (VALI_COMPILER_RT_ASM_FLAGS ${CMAKE_C_FLAGS})
set (VALI_COMPILER_RT_C_FLAGS "${CMAKE_C_FLAGS} -I${CMAKE_CURRENT_LIST_DIR}/../librt/libos/include -I${CMAKE_CURRENT_LIST_DIR}/../librt/libc/include")
set (VALI_COMPILER_RT_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${CMAKE_CURRENT_LIST_DIR}/../librt/libos/include -I${CMAKE_CURRENT_LIST_DIR}/../librt/libc/include")
set (VALI_COMPILER_RT_TARGET clang_rt.builtins-aarch64)

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} some other flags")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
#if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
