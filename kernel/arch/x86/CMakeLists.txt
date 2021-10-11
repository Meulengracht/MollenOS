# Project setup
project (ValiPlatform-X86)
enable_language (C)

if (${VALI_ARCH} STREQUAL "i386")
    add_subdirectory(x32)
    include_directories(x32)
else ()
    add_subdirectory(x64)
    include_directories(x64)
endif ()

# Copy config file
configure_file(arch.h ${CMAKE_BINARY_DIR}/arch.h COPYONLY)

# Configure include paths
include_directories (
        ${CMAKE_BINARY_DIR}
        ../../../librt/libc/include
        ../../../librt/libds/include
        ../../../librt/libddk/include
        ../../../librt/libacpi/source/include
        ../../include
        ../include
        ./
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/smp_trampoline.c
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/file2c ${VALI_PATH_DEPLOY}/ap.sys ${CMAKE_BINARY_DIR}/smp_trampoline.c __GlbTramplineCode
    DEPENDS tools ap
)

add_library(vali-platform STATIC
    ${CMAKE_BINARY_DIR}/smp_trampoline.c
    components/cmos.c
    components/cpu.c
    components/io.c
    components/memory.c
    components/mp.c
    components/output.c
    components/pit.c
    components/rtc.c
    components/smbios.c
    components/smp.c
    components/thread.c
    components/timers.c
    
    interrupts/api.c
    interrupts/exceptions.c
    interrupts/init.c
    interrupts/apic/apichandlers.c
    interrupts/apic/apichelpers.c
    interrupts/apic/apicinit.c
    interrupts/apic/apicio.c
    interrupts/apic/apicipi.c
    interrupts/pic/pic.c
    
    idt_stubs.c
)
target_compile_options(vali-platform
    PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse -DUNICODE -DLIBC_KERNEL -D_KRNL_DLL -D__LIBDS_KERNEL__>
)
add_dependencies (vali-platform vali-${VALI_ARCH})
