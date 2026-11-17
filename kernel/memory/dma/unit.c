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
 * IOMMU units. An IOMMU unit is the kernel's record of one IOMMU hardware
 * block. It holds the limits of that block and the backend callbacks that
 * program it. Several address spaces can share one unit. A unit that has
 * failed refuses all new work.
 */

#include <debug.h>
#include <heap.h>
#include "private.h"

// The IOMMU maps memory in blocks called granules. The smallest granule this
// code accepts is 4 KiB.
#define __DMA_IOMMU_MIN_GRANULE 4096ULL

oserr_t
DMAIOMMUUnitCreate(
    _In_    const DMAIOMMUUnitConfig_t* config,
    _In_    const DMAIOMMUBackendOps_t* ops,
    _In_    void*                      unitContext,
    _InOut_ DMAIOMMUUnit_t**            unitOut)
{
    DMAIOMMUUnit_t* unit;

    if (unitOut == NULL) {
        ERROR("DMAIOMMUUnitCreate: unit create called without an output pointer");
        return OS_EINVALPARAMS;
    }

    if (*unitOut != NULL) {
        ERROR("DMAIOMMUUnitCreate: unit create called with an existing unit %p", (void*)*unitOut);
        return OS_EBUSY;
    }

    // The granule must be a power of two. The address and physical limits must
    // each be large enough for the first granule, which starts at address zero.
    // Every callback is required.
    if (config == NULL || ops == NULL ||
        config->Granularity < __DMA_IOMMU_MIN_GRANULE ||
        (config->Granularity & (config->Granularity - 1)) != 0 ||
        config->AddressLimit < config->Granularity - 1 ||
        config->PhysicalLimit < config->Granularity - 1 ||
        config->CompletionTimeoutNs == 0 ||
        ops->CreateSpace == NULL || ops->DestroySpace == NULL ||
        ops->PrepareMapping == NULL || ops->InstallMapping == NULL ||
        ops->RemoveMapping == NULL || ops->DestroyMapping == NULL) {
        ERROR("DMAIOMMUUnitCreate: invalid unit config %p or backend ops %p", (void*)config, (void*)ops);
        return OS_EINVALPARAMS;
    }

    unit = kmalloc(sizeof(DMAIOMMUUnit_t));
    if (unit == NULL) {
        ERROR("DMAIOMMUUnitCreate: failed to allocate a unit");
        return OS_EOOM;
    }

    // Config and ops are copied, so the backend can free its own copies after
    // this call returns.
    MutexConstruct(&unit->Lock, MUTEX_FLAG_PLAIN);
    atomic_init(&unit->Failure, OS_EOK);
    unit->Spaces = 0;
    unit->Config = *config;
    unit->Ops = *ops;
    unit->Context = unitContext;

    *unitOut = unit;
    return OS_EOK;
}

void
DMAIOMMUUnitFail(
    _In_ DMAIOMMUUnit_t* unit,
    _In_ oserr_t         reason)
{
    oserr_t expected = OS_EOK;

    if (unit == NULL || reason == OS_EOK) {
        ERROR("DMAIOMMUUnitFail: invalid unit failure report %p: %u", (void*)unit, reason);
        return;
    }

    // A backend callback may already hold Lock, and a fault handler on
    // another thread can report a failure as well. So the reason is stored
    // with an atomic compare and never takes the lock. Only the first reason
    // is kept. New work checks this value and is refused once it is set.
    if (atomic_compare_exchange_strong(&unit->Failure, &expected, reason)) {
        ERROR("DMAIOMMUUnitFail: unit %p failed: %u", (void*)unit, reason);
    }
}

oserr_t
DMAIOMMUUnitDestroy(
    _InOut_ DMAIOMMUUnit_t** unit)
{
    DMAIOMMUUnit_t* released;

    if (unit == NULL || *unit == NULL) {
        return OS_EOK;
    }

    released = *unit;
    
    MutexLock(&released->Lock);
    if (released->Spaces != 0) {
        WARNING("DMAIOMMUUnitDestroy: unit %p still has %u address spaces",
                (void*)released, released->Spaces);
        MutexUnlock(&released->Lock);
        return OS_EBUSY;
    }
    MutexUnlock(&released->Lock);

    // The caller must already have stopped everything that can still use the
    // unit. The space count only catches spaces that still exist. It cannot
    // tell whether another thread is still inside a call on this unit.
    MutexDestruct(&released->Lock);
    kfree(released);
    *unit = NULL;
    return OS_EOK;
}
