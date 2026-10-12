/**
 * Copyright, Philip Meulengracht
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
 */

#include <arch/aarch64/arch.h>
#include <arch/utils.h>
#include <arch/interrupts.h>
#include <component/cpu.h>
#include <ddk/ddkdefs.h>
#include <machine.h>
#include <os/types/shm.h>
#include <os/types/query.h>
#include <string.h>

#include "../../../components/cpu_private.h"
#include "private.h"

// Slots 0..15 implement the reserved TLS ABI. The logical ID is privileged
// per-CPU data and is independent of the user's TPIDR_EL0/x18 TLS anchor.
struct Arm64CpuLocal g_arm64CpuLocals[ARM64_CPU_COUNT];

// Scheduling can release the outgoing thread's last handle. The janitor may
// then reclaim its kernel stack on another CPU before exception dispatch has
// returned. Dispatch IRQs and kernel yields on storage owned by the CPU.
static unsigned char g_interruptStacks[ARM64_CPU_COUNT][ARM64_INTERRUPT_STACK_SIZE]
    __attribute__((aligned(ARM64_STACK_ALIGNMENT)));

_Static_assert(
    offsetof(struct Arm64CpuLocal, InterruptStackTop) == ARM64_LOCAL_INTERRUPT_STACK,
    "ARM64 vector dispatch stack offset");

void
Arm64InitializeLocal(
    _In_ unsigned int coreId)
{
    uintptr_t    anchor = (uintptr_t)&g_arm64CpuLocals[coreId];
    uint64_t     processorFeatures;
    uint64_t     instructionFeatures;
    uint64_t     control;
    unsigned int features = 0;

    g_arm64CpuLocals[coreId].Id = coreId;
    g_arm64CpuLocals[coreId].InterruptStackTop =
        (uintptr_t)&g_interruptStacks[coreId][sizeof(g_interruptStacks[coreId])];
    
    // TPIDR_EL1 holds our privileged per-core pointer, not user TLS. The "r"
    // operand uses a compiler-chosen 64-bit general register. ISB makes the
    // new register context visible before subsequent code uses the anchor;
    // the memory clobber prevents compiler reordering across its installation.
    __asm__ volatile("msr tpidr_el1, %0\nisb" :: "r"(anchor) : "memory");
    
    // MRS reads feature/control registers at EL1: user code cannot assume
    // access to these ID registers, so libc asks the kernel for capabilities.
    // "=r" captures each read in a 64-bit compiler-chosen general register.
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(processorFeatures));
    __asm__ volatile("mrs %0, id_aa64isar2_el1" : "=r"(instructionFeatures));
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(control));
    
    // ID_AA64PFR0_EL1.AdvSIMD[23:20] is a signed 4-bit field: 0 means
    // Advanced SIMD, 1 adds FP16, and 0xf means absent. Testing <8 accepts
    // nonnegative feature levels, rather than treating zero as unsupported.
    // CPACR_EL1.FPEN[21:20]=0b11 permits FP/SIMD at both EL0 and EL1. Hardware
    // support alone is insufficient: the OS must enable access/save state.
    if (((processorFeatures >> ARM64_PFR0_SIMD_SHIFT) & ARM64_FEATURE_FIELD_MASK) < ARM64_PFR0_SIMD_SIGN_BIT &&
        (control & ARM64_CPACR_FP_MASK) == ARM64_CPACR_FP_ENABLE) {
        features |= OSSYSTEMCPUFEATURE_NEON;
    }
    
    // ID_AA64ISAR2_EL1.MOPS[19:16]=1 advertises memory copy/set instructions;
    // zero means absent. Reject other encodings rather than assuming support.
    if (((instructionFeatures >> ARM64_ISAR2_MOPS_SHIFT) & ARM64_FEATURE_FIELD_MASK) == ARM64_ISAR2_MOPS_SUPPORTED) {
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(control));
        // SCTLR_EL1.MSCEn[33]=1 permits CPY*/SET* execution at EL0. Preserve
        // unrelated MMU/cache/control bits, then synchronize the write with
        // ISB before publishing the capability. EL2/EL3 permissions must
        // already have been established by the handoff/firmware contract.
        control |= ARM64_SCTLR_MOPS_ENABLE;
        __asm__ volatile("msr sctlr_el1, %0\nisb" :: "r"(control) : "memory");
        features |= OSSYSTEMCPUFEATURE_MOPS;
    }
    
    // Publish only after this core's execution controls have been applied.
    atomic_store_explicit(&g_arm64CpuLocals[coreId].UserCpuFeatures,
        features, memory_order_release);
}

unsigned int
Arm64GetCpuFeatures(void)
{
    unsigned int features = OSSYSTEMCPUFEATURE_NEON | OSSYSTEMCPUFEATURE_MOPS;
    size_t       coreCount = GetMachine()->NumberOfCores;
    size_t       coreId;

    if (coreCount == 0 || coreCount > ARM64_CPU_COUNT) {
        return 0;
    }
    
    // A process caches its memcpy choice and can migrate. Intersect all
    // configured cores, not just this CPU; a zero-initialized/not-yet-ready
    // slot conservatively removes all acceleration. Acquire pairs with each
    // core's release publication. CPU hotplug is outside this fixed-set ABI.
    for (coreId = 0; coreId < coreCount; coreId++) {
        features &= atomic_load_explicit(&g_arm64CpuLocals[coreId].UserCpuFeatures,
            memory_order_acquire);
    }
    return features;
}

uuid_t
ArchGetProcessorCoreId(void)
{
    struct Arm64CpuLocal* local;
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(local));
    return local->Id;
}

void
ArchPlatformInitialize(
    _In_ SystemCpu_t*     cpu,
    _In_ SystemCpuCore_t* core)
{
    uint64_t affinity;

    // MPIDR supplies the core's firmware affinity, MIDR its model/revision,
    // and CNTFRQ the common physical-counter frequency in ticks per second.
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(affinity));
    __asm__ volatile("mrs %0, midr_el1" : "=r"(cpu->PlatformData.Midr));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cpu->PlatformData.CounterFrequency));
    
    core->Id = 0;
    core->PlatformData.Affinity = affinity & ARM64_MPIDR_AFFINITY_MASK;
    
    memcpy(cpu->Vendor, "Arm", sizeof("Arm"));
    memcpy(cpu->Brand, "ARMv8-A", sizeof("ARMv8-A"));
    
    g_arm64CpuLocals[0].Reserved[0] = (uintptr_t)core;
}

void
SetMachineUmaMode(void)
{
    GetMachine()->NumberOfCores = GetMachine()->Processor.NumberOfCores;
    GetMachine()->NumberOfActiveCores = 1;
}

void
ArchProcessorIdle(void)
{
    // The initial idle context inherits the loader's interrupt masking.
    // Every idle entry must allow timer and IPI wakeups before sleeping.
    __asm__ volatile("msr daifclr, %0\nwfi" :: "i"(ARM64_DAIF_IRQ) : "memory");
}

void
ArchProcessorHalt(void)
{
    // Mask Debug, SError, IRQ and FIQ before entering the permanent wait loop.
    __asm__ volatile("msr daifset, %0" :: "i"(ARM64_DAIF_ALL) : "memory");
    for (;;) {
        __asm__ volatile("wfe" ::: "memory");
    }
}

oserr_t
ArchSHMTypeToPageMask(
    _In_ enum OSMemoryConformity conformity,
    _Out_ size_t*                maskOut)
{
    switch (conformity) {
        case OSMEMORYCONFORMITY_NONE:
        case OSMEMORYCONFORMITY_BITS64:
            *maskOut = UINT64_MAX;
            return OS_EOK;
        case OSMEMORYCONFORMITY_LEGACY:
            *maskOut = ARM64_MEMORY_MASK_LEGACY;
            return OS_EOK;
        case OSMEMORYCONFORMITY_LOW:
            *maskOut = ARM64_MEMORY_MASK_LOW;
            return OS_EOK;
        case OSMEMORYCONFORMITY_BITS32:
            *maskOut = ARM64_MEMORY_MASK_32;
            return OS_EOK;
        default:
            return OS_ENOTSUPPORTED;
    }
}

void
CpuFlushInstructionCache(
    _In_ void*  start,
    _In_ size_t length)
{
    uint64_t  ctr;
    uintptr_t address;
    uintptr_t end = (uintptr_t)start + length;
    size_t    dataLine;
    size_t    instructionLine;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    
    // CTR.DminLine[19:16] and IminLine[3:0] encode log2(words per cache line).
    // Clean changed data to the point of unification before invalidating the
    // instruction cache, so instruction fetches see the newly written bytes.
    dataLine = ARM64_CACHE_WORD_SIZE << ((ctr >> ARM64_CTR_DATA_SHIFT) & ARM64_FEATURE_FIELD_MASK);
    instructionLine = ARM64_CACHE_WORD_SIZE << (ctr & ARM64_FEATURE_FIELD_MASK);
    
    for (address = (uintptr_t)start & ~(dataLine - 1); address < end; address += dataLine) {
        __asm__ volatile("dc cvau, %0" :: "r"(address) : "memory");
    }
    
    __asm__ volatile("dsb ish" ::: "memory");
    
    for (address = (uintptr_t)start & ~(instructionLine - 1); address < end; address += instructionLine) {
        __asm__ volatile("ic ivau, %0" :: "r"(address) : "memory");
    }
    
    __asm__ volatile("dsb ish\nisb" ::: "memory");
}

void
CpuInvalidateMemoryCache(
    _In_ void*  start,
    _In_ size_t length)
{
    // This kernel interface is used for translation shootdowns, including
    // mappings that are already absent. Do not dereference or DC-maintain them.
    (void)start;
    (void)length;
    
    __asm__ volatile("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb" ::: "memory");
}

size_t
CpuDataCacheLineSize(void)
{
    uint64_t ctr;

    // CTR.DminLine is the smallest data line of any cache in the system, so
    // stepping by it never skips a line in a larger cache.
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    return ARM64_CACHE_WORD_SIZE << ((ctr >> ARM64_CTR_DATA_SHIFT) & ARM64_FEATURE_FIELD_MASK);
}

enum __DataCacheOperation {
    __DataCacheClean,
    __DataCacheInvalidate,
    __DataCacheCleanInvalidate
};

/**
 * @brief Apply one cache operation to every line touching a block of RAM.
 *
 * The "to point of coherency" forms are used because that is where devices
 * read and write memory. One loop serves all three operations so they cannot
 * differ in how they round to whole lines.
 */
static void
__MaintainDataCache(
    _In_ uintptr_t                 physical,
    _In_ size_t                    length,
    _In_ enum __DataCacheOperation operation)
{
    size_t    line;
    uintptr_t address;
    uintptr_t end = physical + length;

    if (length == 0) {
        return;
    }

    // Every address space links the boot identity window, where RAM sits at
    // the same virtual address as its physical one, so no mapping is needed.
    line = CpuDataCacheLineSize();
    for (address = physical & ~(line - 1); address < end; address += line) {
        switch (operation) {
            case __DataCacheClean:
                __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
                break;
            case __DataCacheInvalidate:
                __asm__ volatile("dc ivac, %0" :: "r"(address) : "memory");
                break;
            default:
                __asm__ volatile("dc civac, %0" :: "r"(address) : "memory");
                break;
        }
    }

    // Wait for every line to finish before a device is told to use the memory,
    // or before the CPU reads what a device wrote.
    __asm__ volatile("dsb sy" ::: "memory");
}

void
CpuDataCacheClean(
    _In_ uintptr_t physical,
    _In_ size_t    length)
{
    __MaintainDataCache(physical, length, __DataCacheClean);
}

void
CpuDataCacheInvalidate(
    _In_ uintptr_t physical,
    _In_ size_t    length)
{
    __MaintainDataCache(physical, length, __DataCacheInvalidate);
}

void
CpuDataCacheCleanInvalidate(
    _In_ uintptr_t physical,
    _In_ size_t    length)
{
    __MaintainDataCache(physical, length, __DataCacheCleanInvalidate);
}
