# Project setup
project (ValiPlatform-X86)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set (CMAKE_ASM_NASM_OBJECT_FORMAT bin)
set (CMAKE_ASM_NASM_FLAGS "-D${VALI_ARCH} -D__${VALI_ARCH}__")

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
        ../../../librt/include
        ../../../librt/libc/include
        ../../../librt/libds/include
        ../../../librt/libddk/include
        ../../../librt/libacpi/source/include
        ../../include
        ../include
        ./)

# Add support for .S extension
set_source_files_properties(min.S PROPERTIES LANGUAGE ASM_NASM)

add_library(vali-platform
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
target_link_libraries (vali-platform vali-${VALI_ARCH})
install(TARGETS vali-platform LIBRARY DESTINATION lib)
