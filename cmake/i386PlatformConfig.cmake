# Configuration file for the i386 platform, specifies compile options
# and the projects needed for this target
if (NOT DEFINED VALI_BUILD)
    message (FATAL_ERROR "You must invoke the root cmake file, not the individual platform files")
endif ()

set (ARCH_FLAGS "-march=i686 -m32 --target=i386-uml-vali")
set (WARNINGS_FLAGS "-Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function")
set (SHARED_FLAGS "-fms-extensions -Wall -ffreestanding -nostdlib -nostdinc -O3")

set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_MMIO")
set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_ACPI_SUPPORT")
set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_UART")
set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_DWCAS")# Assume presence of CPUID_FEAT_EDX_CX8 in cpuid
if (NOT VALI_HEADLESS)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_HAS_VIDEO")
endif ()

set (ASM_FLAGS "-f win32 -Xvc")
set (VALI_TARGET_TRIPPLE "i386-uml-vali")

set (CMAKE_ASM_NASM_OBJECT_FORMAT win32)
set (NASM_DEFAULT_FORMAT win32)
set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set (CMAKE_ASM_NASM_FLAGS "-Xvc -D${VALI_ARCH} -D__${VALI_ARCH}__ ${FEATURE_FLAGS}")

set (CMAKE_C_FLAGS "${ARCH_FLAGS} ${SHARED_FLAGS} ${WARNINGS_FLAGS} ${FEATURE_FLAGS}")
set (CMAKE_CXX_FLAGS "${ARCH_FLAGS} -std=c++17 ${SHARED_FLAGS} ${WARNINGS_FLAGS} ${FEATURE_FLAGS}")

set (VALI_COMPILER_RT_ASM_FLAGS ${CMAKE_C_FLAGS})
set (VALI_COMPILER_RT_C_FLAGS "${CMAKE_C_FLAGS} -I${CMAKE_CURRENT_LIST_DIR}/../librt/include -I${CMAKE_CURRENT_LIST_DIR}/../librt/libc/include")
set (VALI_COMPILER_RT_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${CMAKE_CURRENT_LIST_DIR}/../librt/include -I${CMAKE_CURRENT_LIST_DIR}/../librt/libc/include")
set (VALI_COMPILER_RT_TARGET clang_rt.builtins-i386)

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} some other flags")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

set (CMAKE_SHARED_LINKER_FLAGS "/lldvpe")
set (CMAKE_EXE_LINKER_FLAGS "/lldvpe")

#if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
