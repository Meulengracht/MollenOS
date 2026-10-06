# aarch64 uses Clang for assembling
enable_language(ASM)

# The GIC port currently acknowledges one active interrupt per CPU and keeps
# DAIF.I masked throughout handling. Nested handling needs its own token stack.
if (VALI_ENABLE_NESTED_IRQS)
    message(FATAL_ERROR "AArch64 does not yet support VALI_ENABLE_NESTED_IRQS")
endif ()

add_kernel_library(vali-aarch64 ""
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
    common/interrupts.c
    common/timer.c
    common/output.c
    rpi/platform.c
)
