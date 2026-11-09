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

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <interrupts/interrupts.h>
#include <component/cpu.h>
#include <debug.h>
#include <machine.h>
#include <string.h>

#include "private.h"
#include "gic.h"
#include "platform.h"

static const struct Arm64GicOperations* g_gic;
static unsigned int                     g_interruptCount;
static Spinlock_t                       g_gicLock;

oserr_t
Arm64GicInitializeCore(void)
{
    PlatformCpuCoreBlock_t* core = CpuCorePlatformBlock(CpuCoreCurrent());
    oserr_t                 status = g_gic->InitializeCore();

    // Only mark the CPU ready for IPIs once its GIC interface works. The release
    // store pairs with the acquire load done by the sender.
    if (status == OS_EOK) {
        __atomic_store_n(&core->Gic.Ready, 1, __ATOMIC_RELEASE);
    }
    return status;
}

oserr_t
PlatformInterruptInitialize(void)
{
    oserr_t status;

    if (g_arm64Platform.Gic.Version == 2) {
        g_gic = &g_arm64GicV2;
    } else if (g_arm64Platform.Gic.Version == 3) {
        g_gic = &g_arm64GicV3;
    } else {
        return OS_ENOTSUPPORTED;
    }

    SpinlockConstruct(&g_gicLock);
    status = g_gic->Initialize(&g_interruptCount);
    if (status != OS_EOK) {
        return status;
    }
    __atomic_store_n(
        &CpuCorePlatformBlock(
            CpuCoreCurrent()
        )->Gic.Ready,
        1,
        __ATOMIC_RELEASE
    );
    return Arm64PlatformInterruptInitialize(g_interruptCount);
}

void
InterruptSetMode(
    int mode)
{
    (void)mode;
}

oserr_t
InterruptResolve(
    _In_  InterruptDescriptor_t* interrupt,
    _In_  unsigned int           flags,
    _Out_ uuid_t*                tableIndex)
{
    if (flags & INTERRUPT_MSI) {
        return OS_ENOTSUPPORTED;
    }
    
    if (flags & INTERRUPT_SOFT) {
        int candidates[ARM64_GIC_ALLOCATABLE_SGI_COUNT];

        for (unsigned int i = 0; i < ARM64_GIC_ALLOCATABLE_SGI_COUNT; i++) {
            candidates[i] = i + ARM64_GIC_ALLOCATABLE_SGI_BASE;
        }
        
        *tableIndex = InterruptGetLeastLoaded(
            candidates,
            ARM64_GIC_ALLOCATABLE_SGI_COUNT
        );
        return *tableIndex == UUID_INVALID ? OS_EOOM : OS_EOK;
    }
    
    if (interrupt->Line < (int)ARM64_GIC_PPI_BASE || interrupt->Line >= (int)g_interruptCount) {
        return OS_EINVALPARAMS;
    }
    *tableIndex = interrupt->Line;
    return OS_EOK;
}

uint32_t
InterruptMessageVectorCount(void)
{
    return 0;
}

oserr_t
InterruptMessageVector(
    _In_ uint32_t               ordinal,
    _In_ InterruptDescriptor_t* descriptor,
    _In_ InterruptMessage_t*    message)
{
    (void)ordinal;
    (void)descriptor;
    (void)message;
    return OS_ENOTSUPPORTED;
}

oserr_t
InterruptConfigure(
    _In_ const InterruptControllerResource_t* interrupt,
    _In_ int                                  enable)
{
    oserr_t status;

    if (interrupt->Flags & INTERRUPT_SOFT) {
        return OS_EOK;
    }
    if (interrupt->Line < ARM64_GIC_PPI_BASE || interrupt->Line >= g_interruptCount) {
        return OS_EINVALPARAMS;
    }

    // A trigger register is shared by several interrupts, so the read/modify/write
    // is done under a lock. IRQs are disabled while we hold it, so a local IRQ
    // never ends up waiting on the lock.
    SpinlockAcquireIrq(&g_gicLock);
    status = g_gic->Configure(interrupt, enable);
    SpinlockReleaseIrq(&g_gicLock);
    return status;
}

void
Arm64EnableTimerInterrupt(void)
{
    g_gic->EnablePrivate(g_arm64Platform.TimerInterrupt);
}

Context_t*
Arm64HandleInterrupt(
    _In_ Context_t* context)
{
    struct Arm64CpuLocal* local = &g_arm64CpuLocals[ArchGetProcessorCoreId()];
    uint32_t              token;
    uint32_t              line;

    // Reading IAR marks the highest-priority eligible interrupt active on this
    // CPU. Read it once and save the token, as we must return that same token
    // when the handler finishes.
    token = g_gic->Acknowledge();

    // Only GICv2 has sender bits in the token. GICv3 returns the full interrupt
    // ID, and masking that to 10 bits would make an LPI look like some other
    // device interrupt.
    line = g_gic->Decode(token);
    if (line >= ARM64_GIC_SPECIAL_ID_BASE && line <= 1023) {
        return context;
    }
    
    if (line >= g_interruptCount) {
        // The interrupt became active when IAR was read, even though we cannot
        // handle the ID. Complete it before reporting the error, and never use
        // the ID as an index into the handler table.
        __asm__ volatile("dsb sy" ::: "memory");
        g_gic->Complete(token);
        FATAL(FATAL_SCOPE_KERNEL, "Unsupported GIC interrupt %u", line);
        return context;
    }

    // Save the full token on the receiving CPU so InterruptsAcknowledge can
    // return it unchanged to EOIR, including the sender bits for software
    // interrupts. IRQ nesting is disabled in this port, so another handler
    // cannot replace this CPU's token before the current one is completed.
    local->InterruptToken = token;

    // The shared kernel calls the scheduler interrupt INTERRUPT_LAPIC. On ARM
    // this is an SGI, handled here without going through the device-handler list.
    // The physical timer has its own registered handler.
    if (line == INTERRUPT_LAPIC) {
        // Register the interrupted context before scheduling, so the scheduler
        // can save the outgoing thread and choose a different context to resume.
        CpuCoreEnterInterrupt(context, 0);

        // Account time already spent running and update the next timer deadline.
        // Zero selects request-driven scheduling rather than timer preemption.
        Arm64TimerAdvance(0);

        // Complete the GIC interrupt even if scheduling selected another
        // thread. EOIR must receive the saved token before that thread resumes.
        InterruptsAcknowledge(INTERRUPT_LAPIC, INTERRUPT_LAPIC);

        // Finish interrupt bookkeeping and return the selected context, which
        // need not belong to the thread that received this scheduling request.
        return CpuCoreExitInterrupt(context, 0);
    }

    // The shared dispatcher handles the remaining IDs, including the physical
    // timer. It records the context, runs the handlers and calls
    // InterruptsAcknowledge, so we must not repeat those steps here.
    return InterruptHandle(context, line);
}

void
InterruptsAcknowledge(
    _In_ int      source,
    _In_ uint32_t tableIndex)
{
    struct Arm64CpuLocal* local = &g_arm64CpuLocals[ArchGetProcessorCoreId()];

    // source identifies the registered handler's source, or INTERRUPT_NONE if
    // no handler handled the request. tableIndex is the software table entry.
    // We need the saved IAR token instead, including the sending CPU for GICv2
    // SGIs. IRQs cannot nest here, so the token is still current. Complete it
    // even if no registered handler handled the interrupt.
    (void)source;
    (void)tableIndex;

    // Finish the handler's earlier memory and device-register writes before
    // telling the interrupt controller that handling is complete. A level-
    // triggered device must have its request cleared by the handler, as writing
    // EOIR does not clear the device's interrupt-status register.
    __asm__ volatile("dsb sy" ::: "memory");

    // Return the full token to EOIR. With the combined EOI mode used here, this
    // drops the running priority and clears the active state. A pending request
    // can then be delivered again once priority and CPU IRQ masking allow it.
    g_gic->Complete(local->InterruptToken);

    // Complete the EOIR write before returning to exception exit, which may
    // resume the interrupted thread or a different thread chosen by scheduling.
    __asm__ volatile("dsb sy" ::: "memory");
}

uint32_t
InterruptsGetPriority(void)
{
    return 0;
}

void
InterruptsSetPriority(
    _In_ uint32_t priority)
{
    // Generic priorities in Vali are vector numbers, not GIC PMR values. Handler
    // nesting remains disabled through DAIF, with a fixed GIC PMR threshold.
    (void)priority;
}

irqstate_t
InterruptSaveState(void)
{
    uint64_t state;

    __asm__ volatile("mrs %0, daif" : "=r"(state));
    return state;
}

irqstate_t
InterruptDisable(void)
{
    irqstate_t state = InterruptSaveState();

    // DAIFSet immediate bit one masks IRQs without masking other exceptions.
    __asm__ volatile("msr daifset, %0" :: "i"(ARM64_DAIF_IRQ) : "memory");
    return state;
}

irqstate_t
InterruptEnable(void)
{
    irqstate_t state = InterruptSaveState();

    // DAIFClr immediate bit one unmasks IRQs only.
    __asm__ volatile("msr daifclr, %0" :: "i"(ARM64_DAIF_IRQ) : "memory");
    return state;
}

irqstate_t
InterruptRestoreState(
    _In_ irqstate_t state)
{
    irqstate_t previous = InterruptSaveState();

    __asm__ volatile("msr daif, %0" :: "r"((uint64_t)state) : "memory");
    return previous;
}

int
InterruptIsDisabled(void)
{
    return (InterruptSaveState() & ARM64_DAIF_IRQ_MASK) != 0;
}

oserr_t
ArchProcessorSendInterrupt(
    _In_ uuid_t coreId,
    _In_ uuid_t interruptId)
{
    SystemCpuCore_t* core;
    unsigned int     line = interruptId & ARM64_INTERRUPT_VECTOR_MASK;

    if (coreId >= ARM64_CPU_COUNT || line >= ARM64_GIC_PPI_BASE) {
        return OS_EINVALPARAMS;
    }
    
    core = GetProcessorCore(coreId);
    if (!core || !__atomic_load_n(&CpuCorePlatformBlock(core)->Gic.Ready, __ATOMIC_ACQUIRE)) {
        return OS_ENOENT;
    }
    
    // Publish shared stores before the SGI makes the target CPU observe them.
    __asm__ volatile("dsb ishst" ::: "memory");
    return g_gic->Send(CpuCorePlatformBlock(core), line);
}
