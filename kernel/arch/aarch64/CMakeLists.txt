# aarch64 uses Clang for assembling
enable_language(ASM)

# The GIC port currently acknowledges one active interrupt per CPU and keeps
# DAIF.I masked throughout handling. Nested handling needs its own token stack.
if (VALI_ENABLE_NESTED_IRQS)
    message(FATAL_ERROR "AArch64 does not yet support VALI_ENABLE_NESTED_IRQS")
endif ()

set (AARCH64_SOURCES
    common/entry.S
    common/vectors.S
    common/fp.S
    common/exceptions.c
    common/cpu.c
    common/io.c
    common/smp.c
    common/secondary.S
    common/thread.c
    common/memory.c
    common/device.c
    common/interrupts.c
    common/timer.c
    common/output.c
)

# RPi uses device-tree discovery and adds its SoC interrupt controllers.
if (VALI_PLATFORM STREQUAL "rpi")
    list (APPEND AARCH64_SOURCES
        devicetree/platform.c
        rpi/interrupts.c
        rpi/mip.c
        rpi/mip-firmware.c
        rpi/bcm2711-msi.c
        rpi/bcm2711-msi-firmware.c
    )
else ()
    list (APPEND AARCH64_SOURCES common/platform.c)
endif ()

add_kernel_library(vali-aarch64 "" ${AARCH64_SOURCES})
