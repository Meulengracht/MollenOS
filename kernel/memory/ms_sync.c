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
#include <debug.h>
#include <machine.h>
#include <threading.h>
#include "private.h"

struct MemorySynchronizationObject {
    _Atomic(int) CallsCompleted;
    uuid_t       MemorySpaceHandle;
    uintptr_t    Address;
    size_t       Length;
};

static void
__MemorySyncCallback(
        _In_ void* context)
{
    struct MemorySynchronizationObject* object  = (struct MemorySynchronizationObject*)context;
    MemorySpace_t*                      current = GetCurrentMemorySpace();
    uuid_t                              currentHandle = GetCurrentMemorySpaceHandle();

    // Make sure the current address space is matching
    // If NULL => everyone must update
    // If it matches our parent, we must update
    // If it matches us, we must update
    if (object->MemorySpaceHandle == UUID_INVALID || current->ParentHandle == object->MemorySpaceHandle ||
        currentHandle == object->MemorySpaceHandle) {
        CpuInvalidateMemoryCache((void*)object->Address, object->Length);
    }
    atomic_fetch_add(&object->CallsCompleted, 1);
}

void
MSSync(
        _In_ MemorySpace_t* memorySpace,
        _In_ uintptr_t      address,
        _In_ size_t         size)
{
    // We can easily allocate this object on the stack as the stack is globally
    // visible to all kernel code. This spares us allocation on heap
    struct MemorySynchronizationObject syncObject = {
            .Address        = address,
            .Length         = size,
            .CallsCompleted = 0
    };

    int           numberOfCores;
    int           numberOfActiveCores;
    size_t        timeout = 1000;
    OSTimestamp_t wakeUp;

    // Skip this entire step if there is no multiple cores active
    numberOfActiveCores = atomic_load(&GetMachine()->NumberOfActiveCores);
    if (numberOfActiveCores <= 1) {
        return;
    }

    // Check for global address, in that case invalidate all cores
    if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, address)) {
        syncObject.MemorySpaceHandle = UUID_INVALID; // Everyone must update
    }
    else {
        if (memorySpace->ParentHandle == UUID_INVALID) {
            syncObject.MemorySpaceHandle = GetCurrentMemorySpaceHandle(); // Children of us must update
        }
        else {
            syncObject.MemorySpaceHandle = memorySpace->ParentHandle; // Parent and siblings!
        }
    }

    numberOfCores = ProcessorMessageSend(
            1,
            CpuFunctionCustom,
            __MemorySyncCallback,
            &syncObject,
            1
    );
    SystemTimerGetWallClockTime(&wakeUp);
    while (atomic_load(&syncObject.CallsCompleted) != numberOfCores && timeout > 0) {
        OSTimestampAddNsec(&wakeUp, &wakeUp, 5 * NSEC_PER_MSEC);
        SchedulerSleep(&wakeUp);
        timeout -= 5;
    }

    if (!timeout) {
        ERROR("MSSync timeout trying to synchronize with cores actual %i != target %i",
              atomic_load(&syncObject.CallsCompleted), numberOfCores);
    }
}