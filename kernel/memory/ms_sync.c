/**
 * Copyright 2023, Philip Meulengracht
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
 */

//#define __TRACE

#include <arch/utils.h>
#include <arch/interrupts.h>
#include <debug.h>
#include <interrupts.h>
#include <machine.h>
#include <spinlock.h>
#include "../components/cpu_private.h"
#include "private.h"

// This is a last-resort progress limit, not a clock timeout: an RTC
// reference cannot advance while the BSP is waiting with interrupts disabled.
#define __MSSYNC_SPIN_LIMIT 100000000U

typedef struct MemorySynchronizationObject {
    _Atomic(int) Remaining;
    uintptr_t    Address;
    size_t       Length;
} MemorySynchronizationObject_t;

static Spinlock_t   g_syncLock = OS_SPINLOCK_INIT;
static _Atomic(int) g_syncActive;
static uuid_t       g_syncInterrupt = UUID_INVALID;

void
MemorySpaceSyncProcessPending(void)
{
    MemorySynchronizationObject_t* object;
    irqstate_t                          irqState;
    
    if (!atomic_load(&g_syncActive)) {
        // also avoids accessing core-local state during early boot
        return;
    }
    
    irqState = InterruptDisable();
    object = atomic_exchange(&CpuCoreCurrent()->MemorySync, NULL);
    if (object) {
        // Invalidate even if a different address space is currently active.
        // This needs no handle lookup, allocation, queue lock or scheduling.
        CpuInvalidateMemoryCache((void*)object->Address, object->Length);
        atomic_fetch_sub(&object->Remaining, 1);
    }
    InterruptRestoreState(irqState);
}

static irqstatus_t
__MemorySyncInterrupt(
    _In_ InterruptFunctionTable_t* functions,
    _In_ void*                     context)
{
    _CRT_UNUSED(functions);
    _CRT_UNUSED(context);

    // Process any pending memory synchronization requests
    MemorySpaceSyncProcessPending();
    return IRQSTATUS_HANDLED;
}

void
MemorySpaceSyncInitialize(void)
{
    InterruptDescriptor_t interrupt = { { 0 } };

    interrupt.Line = INTERRUPT_NONE;
    interrupt.Pin = INTERRUPT_NONE;
    interrupt.Vectors[0] = INTERRUPT_NONE;
    interrupt.ResourceTable.Handler = __MemorySyncInterrupt;
    
    g_syncInterrupt = InterruptRegister(
        &interrupt,
        INTERRUPT_SOFT | INTERRUPT_KERNEL | INTERRUPT_EXCLUSIVE
    );
    if (g_syncInterrupt == UUID_INVALID) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to register memory synchronization interrupt");
    }
}

void
MSSync(
    _In_ MemorySpace_t* memorySpace,
    _In_ uintptr_t      address,
    _In_ size_t         size)
{
    // The request remains alive until every target has acknowledged. Delayed
    // IPIs only poll the mailbox, so they never retain a pointer to this stack.
    MemorySynchronizationObject_t syncObject = {
            .Address        = address,
            .Length         = size,
            .Remaining      = 0
    };
    SystemCpuCore_t* self;
    SystemCpuCore_t* core;
    unsigned int     attempts = 0;
    irqstate_t       irqState;
    _CRT_UNUSED(memorySpace);

    // A shared kernel mapping may have been cleared through a different
    // memory-space object, in which case the MMU did not flush this CPU.
    irqState = InterruptDisable();
    CpuInvalidateMemoryCache((void*)address, size);

    // Skip this entire step if there is no multiple cores active
    if (atomic_load(&GetMachine()->NumberOfActiveCores) <= 1) {
        InterruptRestoreState(irqState);
        return;
    }

    if (g_syncInterrupt == UUID_INVALID) {
        FATAL(FATAL_SCOPE_KERNEL, "Memory synchronization interrupt is unavailable");
    }

    SpinlockAcquireIrq(&g_syncLock);
    self = CpuCoreCurrent();
    atomic_store(&g_syncActive, 1);
    
    // enumerate the cores and send memory synchronization interrupts
    for (core = GetMachine()->Processor.Cores; core != NULL; core = CpuCoreNext(core)) {
        if (core == self || !(CpuCoreState(core) & CpuStateRunning)) {
            continue;
        }
        
        atomic_fetch_add(&syncObject.Remaining, 1);
        atomic_store(&core->MemorySync, &syncObject);

        if (ArchProcessorSendInterrupt(CpuCoreId(core), g_syncInterrupt) != OS_EOK) {
            FATAL(FATAL_SCOPE_KERNEL, "Failed to send memory synchronization interrupt");
        }
    }

    // Wait for all cores to acknowledge the memory synchronization request
    while (atomic_load(&syncObject.Remaining) != 0) {
        MemorySpaceSyncProcessPending();
        if (++attempts == __MSSYNC_SPIN_LIMIT) {
            // Never return and reuse pages or the stack while a target still
            // has stale translations or a reference to this request.
            FATAL(FATAL_SCOPE_KERNEL, "MSSync timeout waiting for %i cores",
                  atomic_load(&syncObject.Remaining));
        }
    }
    atomic_store(&g_syncActive, 0);
    SpinlockReleaseIrq(&g_syncLock);
    InterruptRestoreState(irqState);
}
