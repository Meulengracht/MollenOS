/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * AArch64 Architecture Header
 */

#ifndef __VALI_ARCH_AARCH64_H__
#define __VALI_ARCH_AARCH64_H__

#include <os/osdefs.h>
#include <os/context.h>

#define ARCHITECTURE_NAME "aarch64"

// Allocation block size for memory management
#define ALLOCATION_BLOCK_SIZE 0x100000 // 1 mb

// Maximum number of supported interrupts on AArch64 architecture
#define MAX_SUPPORTED_INTERRUPTS 1020

// Page size for AArch64 architecture
#define ARM64_PAGE_SIZE 4096

// Max number of supported CPUs on aarch64
#define ARM64_CPU_COUNT 256

// TTBR0 contains user mappings and the privileged boot identity window.
// TTBR1 contains shared kernel allocations. The final TTBR0 L0 slot is private
// to each thread, matching Vali's separate context and thread-local mappings.
#define MEMORY_LOCATION_SHARED_START       0xffff800000000000ULL
#define MEMORY_LOCATION_SHARED_END         0xffff800040000000ULL
#define MEMORY_LOCATION_RING3_CODE         0x8000000000ULL
#define MEMORY_LOCATION_RING3_CODE_END     0x8100000000ULL
#define MEMORY_LOCATION_RING3_HEAP         0x8100000000ULL
#define MEMORY_LOCATION_RING3_HEAP_END     0x8200000000ULL
#define MEMORY_LOCATION_RING3_THREAD_START 0x0000ff8000000000ULL
#define MEMORY_LOCATION_RING3_THREAD_END   0x0000ff8100000000ULL

// Software-generated interrupts are private to the kernel and below the GIC
// PPI/SPI range. They are not exposed as allocatable device interrupt lines.
#define INTERRUPT_SOFTWARE_BASE 0
#define INTERRUPT_SOFTWARE_END 16
#define INTERRUPT_PHYSICAL_BASE 32
#define INTERRUPT_PHYSICAL_END 1020
#define INTERRUPT_SYSCALL 0
#define INTERRUPT_LAPIC 1

// Affinity mask for extracting the relevant bits from the MPIDR register.
#define ARM64_MPIDR_AFFINITY_MASK 0xff00ffffffULL

typedef struct PlatformCpuBlock {
    // CPU model and revision value read from the processor.
    uint64_t Midr;
    // Timer ticks per second, used to measure time and set timer intervals.
    uint64_t CounterFrequency;
} PlatformCpuBlock_t;

typedef struct PlatformCpuCoreBlock {
    // Hardware ID used to identify this core when starting it.
    uint64_t     Affinity;
    // Address where a core checks for its start instruction when 
    // using that start method.
    uint64_t     ReleaseAddress;
    // Stack address for this core's startup code.
    uintptr_t    BootStack;
    // Selects how the system asks this core to start.
    unsigned int EnableMethod;
    // Value used to direct device interrupts to this core.
    unsigned int GicTarget;
} PlatformCpuCoreBlock_t;

typedef struct PlatformThreadBlock {
    // Saved floating-point state, restored when this thread runs again.
    void*     MathBuffer;
    // Per-thread value made available to user programs when this thread runs.
    uintptr_t UserTls;
} PlatformThreadBlock_t;

typedef struct PlatformMemoryBlock {
    // Physical address of this memory space's mapping tables, used when 
    // switching spaces.
    uintptr_t TablePhysical;
    // Physical address of the shared kernel mapping tables, kept active 
    // with this space.
    uintptr_t KernelTablePhysical;
} PlatformMemoryBlock_t;

/**
 * @brief Returns userspace CPU capabilities common to all configured cores.
 * 
 * @return OSSystemCPUFeatures flags; uninitialized cores contribute no features.
 */
KERNELAPI unsigned int KERNELABI
Arm64GetCpuFeatures(void);

#endif //!__VALI_ARCH_AARCH64_H__
