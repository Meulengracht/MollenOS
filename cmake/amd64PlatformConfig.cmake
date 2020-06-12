# Configuration file for the amd64 platform, specifies compile options
# and the projects needed for this target
if (NOT DEFINED VALI_BUILD)
    message (FATAL_ERROR "You must invoke the root cmake file, not the individual platform files")
endif ()

# Define the projects that we use
set (VALI_PROJECTS
    "boot/CMakeLists.txt"
)

#@$(MAKE) -s -C libddk -f makefile
#@$(MAKE) -s -C libds -f makefile
#@$(MAKE) -s -C libclang -f makefile
#@$(MAKE) -s -C libcrt -f makefile libcrt
#@$(MAKE) -s -C libcrt -f makefile libdrv
#@$(MAKE) -s -C libcrt -f makefile libsrv
#@$(MAKE) -s -C libm -f makefile

# Define compilation options for generic things
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W4")
    # Default debug flags are OK
    set(CMAKE_CXX_FLAGS_RELEASE "{CMAKE_CXX_FLAGS_RELEASE} /O2")
else()
    set (ARCH_FLAGS "-m64 -Damd64 -D__x86_64__ -D__STDC_FORMAT_MACROS_64 --target=amd64-pc-win32-itanium-coff -fdwarf-exceptions")
    set (WARNINGS_FLAGS "-Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function")
    set (SHARED_FLAGS "-U_WIN32 -fms-extensions -Wall -nostdlib -nostdinc -O3 -DMOLLENOS")
    set (FEATURE_FLAGS "${FEATURE_FLAGS} \
    -D__OSCONFIG_HAS_MMIO \
    -D__OSCONFIG_ACPI_SUPPORT \
    -D__OSCONFIG_HAS_UART \
    -D__OSCONFIG_HAS_DWCAS") # Assume presence of CPUID_FEAT_EDX_CX8 in cpuid

    if (VALI_HEADLESS)
        set (FEATURE_FLAGS ${FEATURE_FLAGS} -D__OSCONFIG_HAS_VIDEO)
    endif ()

    set (CMAKE_C_FLAGS "-ffreestanding ${SHARED_FLAGS} ${ARCH_FLAGS} ${WARNINGS_FLAGS} ${FEATURE_FLAGS}")
    set (CMAKE_CXX_FLAGS "-std=c++17 -ffreestanding ${SHARED_FLAGS} ${ARCH_FLAGS} ${WARNINGS_FLAGS} ${FEATURE_FLAGS}")

    #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} some other flags")
    #set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

    set (CMAKE_SHARED_LINKER_FLAGS "/nodefaultlib /machine:X64 /subsystem:native")
    set (CMAKE_EXE_LINKER_FLAGS "/nodefaultlib /machine:X64 /subsystem:native")

    #if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
endif()
