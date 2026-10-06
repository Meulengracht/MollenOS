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

#include <arch/utils.h>
#include <component/cpu.h>
#include <debug.h>
#include <machine.h>
#include "private.h"

/**
 * @brief Carries the information a secondary CPU needs before it can enter C code.
 *
 * Arm64SecondaryEntry searches this array using the CPU's MPIDR affinity.
 * The descriptor, entry code, and startup stack must be accessible before
 * translation is enabled. Field order and size are shared with assembly;
 * the assertions below check that its fixed offsets remain correct.
 */
struct Arm64SecondaryState {
    // Full hardware affinity identifying the CPU, not its kernel-assigned ID.
    uint64_t Affinity;
    // Top of the reserved startup stack; the stack grows toward lower addresses.
    uint64_t Stack;
    // Physical root for TTBR0, retaining the boot identity window.
    uint64_t Table0;
    // Physical root for TTBR1, providing the shared kernel mappings.
    uint64_t Table1;
    // TCR_EL1 settings for address sizes and how hardware reads the tables.
    uint64_t Tcr;
    // MAIR_EL1 settings assigning memory types to descriptor attribute indexes.
    uint64_t Mair;
    // Kernel-assigned CPU ID passed to Arm64SecondaryStart after assembly setup.
    uint64_t Id;
    // Nonzero marks a prepared descriptor for the assembly search, not a CPU
    // that has finished startup. CpuStateRunning supplies that later indication.
    uint64_t Ready;
};

// Keep descriptors and stacks in the reserved kernel image so startup never
// depends on allocating memory from the CPU that is not running yet. Each
// secondary gets its own stack instead of sharing the boot CPU's live stack.
struct Arm64SecondaryState g_arm64SecondaryState[ARM64_CPU_COUNT];
static unsigned char g_secondaryStacks[ARM64_CPU_COUNT][ARM64_SECONDARY_STACK_SIZE]
    __attribute__((aligned(ARM64_PAGE_SIZE)));

// Assembly reads these fields without C type information or a usable stack.
// A layout mismatch would select incorrect roots, stack pointers, or CPU IDs.
_Static_assert(ARM64_CPU_COUNT == ARM64_SECONDARY_SLOTS, "ARM64 secondary slot count");
_Static_assert(sizeof(struct Arm64SecondaryState) == ARM64_SECONDARY_STATE_SIZE, "ARM64 secondary state size");
_Static_assert(offsetof(struct Arm64SecondaryState, Stack) == ARM64_SECONDARY_STACK, "ARM64 secondary stack offset");
_Static_assert(offsetof(struct Arm64SecondaryState, Table0) == ARM64_SECONDARY_TABLES, "ARM64 secondary table offset");
_Static_assert(offsetof(struct Arm64SecondaryState, Tcr) == ARM64_SECONDARY_CONTROLS, "ARM64 secondary control offset");
_Static_assert(offsetof(struct Arm64SecondaryState, Id) == ARM64_SECONDARY_ID, "ARM64 secondary ID offset");
_Static_assert(offsetof(struct Arm64SecondaryState, Ready) == ARM64_SECONDARY_READY, "ARM64 secondary ready offset");

/**
 * @brief Provides the physical startup entry used by spin-table and PSCI firmware.
 *
 * Assembly establishes the stack, exception vectors, EL1 controls, and memory
 * translation before calling Arm64SecondaryStart. Pass its address to firmware;
 * it is not a normal C function to call on the requesting CPU.
 */
extern void Arm64SecondaryEntry(void);

/**
 * @brief Makes both boot translation-table trees visible to a starting CPU.
 *
 * Cleans table cache lines and waits for completion before the secondary is
 * released with its caches disabled. The tables must remain stable during this
 * publication; this operation does not release or start a CPU by itself.
 */
extern void Arm64PrepareSecondaryMemory(void);

/**
 * @brief Samples the physical counter used to bound secondary startup waits.
 *
 * The counter advances independently of scheduler timer interrupts, which
 * need not be enabled on the CPU performing startup.
 *
 * @return Current physical counter value in ticks, not nanoseconds.
 */
static uint64_t
__ReadCounter(void)
{
    uint64_t counter;

    // ISB orders the sample after earlier instructions. CNTPCT_EL0 provides
    // the physical timebase whose frequency is stored in the processor block.
    __asm__ volatile("isb\nmrs %0, cntpct_el0" : "=r"(counter));
    return counter;
}

/**
 * @brief Writes a prepared memory range out of the cache before firmware uses it.
 *
 * Cleans every overlapping data-cache line and waits for those cleans to
 * complete. Needed when the reader is firmware or a CPU with caches disabled.
 * Does not make simultaneous edits safe; the caller finishes writes first.
 *
 * @param address Kernel-accessible beginning of the range to publish.
 * @param size Range length in bytes. The complete range must be mapped and not wrap.
 */
static void
__PublishRange(
    _In_ uintptr_t address,
    _In_ size_t    size)
{
    uint64_t  ctr;
    size_t    line;
    uintptr_t end = address + size;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    // CTR.DminLine gives the cache line size in words. Clean to the point of
    // coherency so firmware and a cache-disabled secondary see the same state.
    line = ARM64_CACHE_WORD_SIZE << ((ctr >> ARM64_CTR_DATA_SHIFT) & ARM64_FEATURE_FIELD_MASK);
    
    // Round down to include the cache line containing the first byte. DC CVAC
    // writes dirty data to the point where other CPUs can observe it; ordinary
    // stores alone could otherwise leave the new entry address only in our cache.
    for (address &= ~(line - 1); address < end; address += line) {
        __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
    }
    
    // Complete all cache maintenance before the caller sends a wakeup event
    // or asks firmware to start a CPU that will consume this range.
    __asm__ volatile("dsb sy" ::: "memory");
}

/**
 * @brief Asks PSCI firmware to start a CPU at the supplied physical entry address.
 *
 * Uses the SMC or HVC interface selected from the device tree. That selection
 * must already be validated; this helper does not probe for firmware support.
 * Firmware acceptance does not mean the CPU has finished kernel initialization.
 *
 * @param affinity Full target CPU affinity expected by PSCI CPU_ON.
 * @param entry Physical entry address reachable before the target enables translation.
 * @param id Kernel CPU ID supplied as firmware context. The assembly entry still
 *           finds its descriptor by affinity rather than trusting this argument.
 * @return Signed PSCI status returned in x0; zero is success, other values are errors.
 */
static int64_t
__PsciCpuOn(
    _in_ uint64_t     affinity,
    _in_ uintptr_t    entry,
    _in_ unsigned int id)
{
    // PSCI's calling convention places function, affinity, entry, and context
    // in x0-x3; x0 returns a signed firmware status, with zero meaning success.
    register uint64_t x0 __asm__("x0") = ARM64_PSCI_CPU_ON;
    register uint64_t x1 __asm__("x1") = affinity;
    register uint64_t x2 __asm__("x2") = entry;
    register uint64_t x3 __asm__("x3") = id;

    // SMC calls the secure monitor; HVC calls the firmware's hypervisor-level
    // interface. Use the advertised method, not a guess based on the CPU model.
    // The operands/clobbers tell the compiler that firmware may change argument
    // and scratch registers or access memory during the call.
    if (g_arm64Platform.PsciConduit == VBOOT_PSCI_SMC) {
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
            :: "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
               "x14", "x15", "x16", "x17", "memory");
    } else {
        __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
            :: "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
               "x14", "x15", "x16", "x17", "memory");
    }
    
    // Preserve negative PSCI error values instead of interpreting them as
    // unsigned success codes or as confirmation that startup completed.
    return (int64_t)x0;
}

/**
 * @brief Prepares and publishes a secondary CPU's assembly startup descriptor.
 *
 * Copies the shared roots and the calling CPU's translation settings, selects
 * the target's reserved stack, and publishes the tables and descriptor. Called
 * on the startup-requesting CPU before the target is released. This is not a
 * live CPU restart or concurrent descriptor-update operation.
 *
 * @param id Registered kernel CPU ID, less than ARM64_CPU_COUNT.
 * @param block Validated platform information for that same target CPU.
 */
static void
__PrepareSecondaryState(
    _In_ unsigned int            id,
    _In_ PlatformCpuCoreBlock_t* block)
{
    struct Arm64SecondaryState* state = &g_arm64SecondaryState[id];

    // Assembly starts without trusting firmware's logical-ID argument. Give it
    // the hardware affinity to match and a separate stack to enter C safely.
    state->Affinity = block->Affinity;
    state->Stack = (uintptr_t)&g_secondaryStacks[id][ARM64_SECONDARY_STACK_SIZE];
    
    // Share the boot/domain roots rather than building a second set of startup
    // mappings. The same roots need matching TCR and MAIR settings so all CPUs
    // agree on table layout and on the memory types encoded in page descriptors.
    state->Table0 = GetDomainMemorySpace()->PlatformData.TablePhysical;
    state->Table1 = GetDomainMemorySpace()->PlatformData.KernelTablePhysical;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(state->Tcr));
    __asm__ volatile("mrs %0, mair_el1" : "=r"(state->Mair));
    
    // Ready lets assembly ignore unused zero-initialized array slots. It is
    // not the completion flag checked by StartApplicationCore. The target must
    // not be released until the following publication steps have finished.
    state->Id = id;
    state->Ready = 1;

    // The target first reads with caches disabled. Publish referenced tables
    // as well as this descriptor before firmware can branch to the entry point.
    Arm64PrepareSecondaryMemory();
    __PublishRange((uintptr_t)state, sizeof(*state));
}

/**
 * @brief Requests secondary startup using its validated firmware enable method.
 *
 * For spin-table firmware, writes and publishes an entry address then signals
 * a wakeup event. For PSCI, checks the CPU_ON response. Neither method waits
 * for CpuStateRunning, and failure does not undo the prepared descriptor.
 *
 * @param id Registered kernel CPU ID, used for logging and PSCI context.
 * @param block Target affinity and start method. A spin-table release word must
 *              be writable through its physical address and naturally aligned.
 * @return One if the release request was issued successfully; zero if unsupported
 *         or rejected by PSCI. One does not confirm that the CPU is running.
 */
static int
__ReleaseSecondary(
    _In_ unsigned int            id,
    _In_ PlatformCpuCoreBlock_t* block)
{
    volatile uint64_t* release;
    int64_t            status;

    // Spin-table firmware waits for its 64-bit release word to contain an entry
    // address. Write the physical entry, not a kernel-only virtual alias that a
    // CPU with translation disabled could not execute.
    if (block->EnableMethod == ARM64_CPU_ENABLE_SPIN_TABLE) {
        release = (void*)(uintptr_t)block->ReleaseAddress;
        *release = (uintptr_t)Arm64SecondaryEntry;
        __PublishRange((uintptr_t)release, sizeof(*release));
        
        // Publish the word before SEV asks a waiting firmware CPU to check it
        // again. SEV sends an event; it is not a scheduler/GIC interrupt and
        // does not itself prove that the target branched to the entry point.
        __asm__ volatile("sev" ::: "memory");
    } else if (block->EnableMethod == ARM64_CPU_ENABLE_PSCI) {
        // PSCI firmware chooses the hardware startup sequence. Its zero status
        // only accepts the request; StartApplicationCore checks kernel progress.
        status = __PsciCpuOn(block->Affinity, (uintptr_t)Arm64SecondaryEntry, id);
        if (status != 0) {
            // Do not wait or retry after a rejected request, including statuses
            // such as already-on. Report the firmware value for diagnosis.
            ERROR("PSCI CPU_ON failed for affinity %llx: %lld", block->Affinity, status);
            return 0;
        }
    } else {
        // No supported release operation exists for this descriptor. Leaving
        // the target stopped is safer than writing an unknown firmware location.
        ERROR("No start method for core %u", id);
        return 0;
    }
    return 1;
}

/**
 * @brief Releases a registered secondary CPU and waits briefly for kernel startup.
 *
 * Prepares state, requests firmware release, and waits at most one second for
 * CpuStateRunning. Logs failures instead of returning a status. A timeout does
 * not cancel startup, clear the descriptor, or reclaim its stack; the CPU may
 * still reach the kernel later. Intended for initial boot, not CPU hotplug.
 *
 * @param core Registered secondary whose ID and platform start information are valid.
 *             Shared memory/GIC setup and a nonzero counter frequency must be ready.
 */
void
StartApplicationCore(
    _In_ SystemCpuCore_t* core)
{
    unsigned int            id = CpuCoreId(core);
    PlatformCpuCoreBlock_t* block = CpuCorePlatformBlock(core);
    uint64_t                deadline;
    int                     released;

    // Finish the descriptor and cache publication before any action can make
    // the target execute code that reads those fields or translation tables.
    __PrepareSecondaryState(id, block);
    released = __ReleaseSecondary(id, block);
    // A rejected or unsupported release cannot be made successful by waiting.
    // The release helper already logs its reason; leave the prepared storage intact.
    if (!released) {
        return;
    }
    
    // The frequency is ticks per second, so adding it gives a one-second
    // deadline without depending on scheduler ticks or enabled local timer IRQs.
    deadline = __ReadCounter() + GetMachine()->Processor.PlatformData.CounterFrequency;
    while (!(CpuCoreState(core) & CpuStateRunning) && __ReadCounter() < deadline) {
        // YIELD is a processor hint for this polling loop, not a call into the
        // scheduler and not a sleep that needs an interrupt to make progress.
        __asm__ volatile("yield" ::: "memory");
    }
    
    // Ready in the descriptor only means assembly may read it. Running is set
    // later by shared CPU startup after its scheduler has been initialized.
    // Keep the stack/state allocated on timeout because a late CPU may use them.
    if (!(CpuCoreState(core) & CpuStateRunning)) {
        ERROR("Core %u did not complete startup", id);
    }
}

/**
 * @brief Completes per-CPU setup after a secondary's assembly entry reaches EL1.
 *
 * Assembly has already installed vectors, a startup stack, translation, and
 * FP/SIMD access with interrupts masked. This establishes kernel-local storage,
 * interrupt reception, and the timer before shared CPU startup creates the
 * scheduler, marks the CPU running, and enables IRQs. Never returns to firmware.
 *
 * @param id Registered kernel CPU ID taken from the affinity-matched descriptor.
 */
_Noreturn void
Arm64SecondaryStart(
    _In_ unsigned int id)
{
    // Install TPIDR_EL1 and the interrupt dispatch stack before any per-CPU
    // helper needs them. Publish this CPU's usable instruction features too.
    Arm64InitializeLocal(id);

    // Reserved TLS slot zero is how shared kernel code obtains CpuCoreCurrent.
    // Install the registered core pointer before GIC and shared startup use it.
    g_arm64CpuLocals[id].Reserved[0] = (uintptr_t)GetProcessorCore(id);

    // GIC private registers and the physical timer are local to this CPU; the
    // boot CPU's initialization cannot substitute for these writes. Keep IRQs
    // masked until shared startup has created the scheduler that handles them.
    Arm64GicInitializeCore();
    Arm64TimerInitializeCore();

    // Shared startup publishes CpuStateRunning and enters the CPU's idle loop.
    // If it unexpectedly returns, halt instead of falling back into firmware or
    // pretending this non-returning entry still has a valid caller to resume.
    CpuCoreStart();
    ArchProcessorHalt();
    __builtin_unreachable();
}
