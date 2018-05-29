# Kernel Project

## Structure

- /acpi (Contains the acpi implementation)
- /arch (Contains implementations of different architectures / architecture specific code)
- /components (Contains the implementation of the system infrastructure)
- /include (Contains include files for the kernel)
- /process (Contains the implementation of systems for userspace processes)
- /synchronization (Contains synchronization primitives for concurrency protection)

The base of this folder contains the remaining of the kernel systems. This includes debugging utilities, 
utility systems and code for scheduler/threading/interrupts on a higher common level that can be shared
on the different architectures.