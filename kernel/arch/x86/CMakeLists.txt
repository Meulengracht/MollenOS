project (vali-kernel-x86)
enable_language (C)

if ("${VALI_ARCH}" STREQUAL "i386")
    add_subdirectory(x32)
    include_directories(x32)
else ()
    add_subdirectory(x64)
    include_directories(x64)
endif ()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/smp_trampoline.c
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/file2c ${VALI_PATH_DEPLOY}/ap.sys ${CMAKE_BINARY_DIR}/smp_trampoline.c __GlbTramplineCode
    DEPENDS tools ap
)

add_kernel_library(vali-x86 ""
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
        components/tsc.c

        interrupts/api.c
        interrupts/exceptions.c
        interrupts/init.c
        interrupts/apic/apichandlers.c
        interrupts/apic/apichelpers.c
        interrupts/apic/apicinit.c
        interrupts/apic/apicio.c
        interrupts/apic/apicipi.c
        interrupts/apic/apictimer.c
        interrupts/pic/pic.c

        idt_stubs.c
)
