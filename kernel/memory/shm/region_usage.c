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
 * Shared access counters and fault tracking for one SHM region. Enforces
 * multiple-reader / single-writer rules across CPU and device sessions,
 * tracks stopped device transfers, and gates region reopening until cache
 * recovery completes.
 */

#include <shm/region_usage.h>
#include <assert.h>
#include <ddk/barrier.h>
#include <debug.h>
#include <limits.h>
#include <string.h>

void
SHMRegionUsageInitialize(
    _Out_ SHMRegionUsage_t* region)
{
    memset(region, 0, sizeof(*region));
    SpinlockConstruct(&region->Lock);
}

// Checks that the token is idle and that generation is the one after the last
// successful use. Returns OS_EINVALPARAMS, OS_EBUSY, OS_EOVERFLOW or OS_ENOENT.
static oserr_t
__CheckNextGeneration(
    _In_ const SHMRegionAccess_t* access,
    _In_ unsigned int             generation)
{
    if (generation == 0) {
        return OS_EINVALPARAMS;
    }

    if (access->State != SHMRegionAccessIdle) {
        return OS_EBUSY;
    }

    if (access->Generation == UINT_MAX) {
        return OS_EOVERFLOW;
    }

    if (generation != access->Generation + 1) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

// Checks that the token is in use and that generation matches its current
// access. Returns OS_EINVALPARAMS or OS_ENOENT.
static oserr_t
__CheckActiveGeneration(
    _In_ const SHMRegionAccess_t* access,
    _In_ unsigned int             generation)
{
    if (generation == 0) {
        return OS_EINVALPARAMS;
    }

    if (access->State == SHMRegionAccessIdle || access->Generation != generation) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

// Hold region->Lock. Decides whether a new reader or writer may start. Returns
// OS_EDEVFAULT if a mapping reported a fault, OS_EBUSY while another access or
// a recovery conflicts, or OS_EOVERFLOW if the reader count has no room left.
static oserr_t
__CheckAccessAllowedLocked(
    _In_ const SHMRegionUsage_t* region,
    _In_ bool                    write)
{
    if (region->Faults != 0) {
        return OS_EDEVFAULT;
    }

    if (region->Writer || region->Recovering || (write && region->Readers != 0)) {
        return OS_EBUSY;
    }

    if (!write && region->Readers == UINT_MAX) {
        return OS_EOVERFLOW;
    }
    return OS_EOK;
}

oserr_t
SHMRegionUsageBegin(
    _In_    SHMRegionUsage_t*  region,
    _InOut_ SHMRegionAccess_t* access,
    _In_    unsigned int       generation,
    _In_    bool               write)
{
    oserr_t status = __CheckNextGeneration(access, generation);

    if (status != OS_EOK) {
        // A busy token is the caller's retry condition, not an error.
        if (status != OS_EBUSY) {
            WARNING("SHMRegionUsageBegin: access refused, generation %u, last generation %u: %u",
                    generation, access->Generation, status);
        }
        return status;
    }

    SpinlockAcquireIrq(&region->Lock);
    status = __CheckAccessAllowedLocked(region, write);
    if (status == OS_EOK) {
        if (write) {
            region->Writer = true;
        } else {
            region->Readers++;
        }

        access->Region = region;
        access->Generation = generation;
        access->Write = write;
        access->State = SHMRegionAccessLive;
        // Order token publication before any caller reads or writes the region.
        dma_mb();
    }
    // Cleanup can run as soon as interrupts resume. Do not touch access again.
    SpinlockReleaseIrq(&region->Lock);

    if (status == OS_EOVERFLOW) {
        ERROR("SHMRegionUsageBegin: region %p cannot admit another reader, the reader count is exhausted",
              (void*)region);
    } else if (status == OS_EDEVFAULT) {
        TRACE("SHMRegionUsageBegin: region %p access refused, a mapping reported a device fault", (void*)region);
    }
    return status;
}

// Hold region->Lock. Removes a live reader or writer from the region counts.
static void
__RemoveLiveAccessLocked(
    _In_ SHMRegionUsage_t*  region,
    _In_ SHMRegionAccess_t* access)
{
    if (access->Write) {
        assert(region->Writer);
        region->Writer = false;
    } else {
        assert(region->Readers != 0);
        region->Readers--;
    }
}

oserr_t
SHMRegionUsageEnd(
    _InOut_ SHMRegionAccess_t* access,
    _In_    unsigned int       generation)
{
    SHMRegionUsage_t* region;
    oserr_t           status;

    status = __CheckActiveGeneration(access, generation);
    if (status != OS_EOK) {
        WARNING("SHMRegionUsageEnd: no access to end for generation %u, current generation %u: %u",
                generation, access->Generation, status);
        return status;
    }

    region = access->Region;
    SpinlockAcquireIrq(&region->Lock);
    // Ensure CPU or device memory accesses finish before releasing the slot.
    dma_mb();
    if (access->State == SHMRegionAccessStopped) {
        assert(region->Stopped != 0);
        region->Stopped--;
    } else {
        __RemoveLiveAccessLocked(region, access);
    }

    // Leave Generation set so the next Begin must advance it and late calls
    // carrying this generation fail with OS_ENOENT.
    access->State = SHMRegionAccessIdle;
    access->Region = NULL;
    access->Write = false;
    SpinlockReleaseIrq(&region->Lock);
    return OS_EOK;
}

oserr_t
SHMRegionUsageStop(
    _InOut_ SHMRegionAccess_t* access,
    _In_    unsigned int       generation)
{
    SHMRegionUsage_t* region;
    oserr_t           status;

    status = __CheckActiveGeneration(access, generation);
    if (status != OS_EOK) {
        WARNING("SHMRegionUsageStop: no access to stop for generation %u, current generation %u: %u",
                generation, access->Generation, status);
        return status;
    }

    if (access->State == SHMRegionAccessStopped) {
        return OS_EOK;
    }

    region = access->Region;
    SpinlockAcquireIrq(&region->Lock);
    // Move the confirmed-stopped transfer out of Readers/Writer into Stopped.
    // Once live users drain, cache recovery can run even while stopped tokens
    // wait to retire, and Stopped keeps the last fault from clearing early.
    if (region->Faults == 0) {
        status = OS_EINVALPARAMS;
    } else if (region->Stopped == UINT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        __RemoveLiveAccessLocked(region, access);
        region->Stopped++;
        access->State = SHMRegionAccessStopped;
    }
    SpinlockReleaseIrq(&region->Lock);

    if (status == OS_EINVALPARAMS) {
        WARNING("SHMRegionUsageStop: region %p has no fault installed, access cannot be stopped", (void*)region);
    } else if (status == OS_EOVERFLOW) {
        ERROR("SHMRegionUsageStop: region %p cannot stop another access, the stopped count is exhausted",
              (void*)region);
    }
    return status;
}

oserr_t
SHMRegionUsageFault(
    _In_    SHMRegionUsage_t* region,
    _InOut_ SHMRegionFault_t* fault)
{
    oserr_t status = OS_EOK;

    if (fault->Region != NULL) {
        if (fault->Region != region) {
            WARNING("SHMRegionUsageFault: fault entry already belongs to region %p, not region %p",
                    (void*)fault->Region, (void*)region);
            return OS_EINVALPARAMS;
        }
        return OS_EOK;
    }

    SpinlockAcquireIrq(&region->Lock);
    if (region->Faults == UINT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        region->Faults++;
        fault->Region = region;
    }
    SpinlockReleaseIrq(&region->Lock);

    if (status == OS_EOVERFLOW) {
        ERROR("SHMRegionUsageFault: region %p cannot record another fault, the fault count is exhausted",
              (void*)region);
    } else {
        TRACE("SHMRegionUsageFault: fault recorded on region %p", (void*)region);
    }
    return status;
}

oserr_t
SHMRegionUsageClearFault(
    _InOut_ SHMRegionFault_t* fault)
{
    SHMRegionUsage_t* region = fault->Region;
    oserr_t           status = OS_EOK;

    if (region == NULL) {
        return OS_EOK;
    }

    SpinlockAcquireIrq(&region->Lock);
    assert(region->Faults != 0);
    // Keep at least one fault mark while stopped tokens or cache recovery are
    // still pending so new Begin calls cannot enter the region prematurely.
    if (region->Faults == 1 && (region->Stopped != 0 || region->Recovering)) {
        status = OS_EBUSY;
    } else {
        region->Faults--;
        fault->Region = NULL;
    }
    SpinlockReleaseIrq(&region->Lock);

    if (status == OS_EBUSY) {
        WARNING("SHMRegionUsageClearFault: region %p still has stopped accesses or a recovery, last fault kept",
                (void*)region);
    } else {
        TRACE("SHMRegionUsageClearFault: fault cleared on region %p", (void*)region);
    }
    return status;
}

oserr_t
SHMRegionUsageBeginRecovery(
    _In_ SHMRegionUsage_t* region)
{
    oserr_t status = OS_EOK;

    SpinlockAcquireIrq(&region->Lock);
    // Wait until live readers and writers finish. Stopped tokens do not block
    // recovery because their hardware can no longer touch RAM.
    if (region->Faults == 0) {
        status = OS_EINVALPARAMS;
    } else if (region->Writer || region->Readers != 0 || region->Recovering) {
        status = OS_EBUSY;
    } else {
        region->Recovering = true;
    }
    SpinlockReleaseIrq(&region->Lock);

    if (status == OS_EINVALPARAMS) {
        WARNING("SHMRegionUsageBeginRecovery: region %p has no fault installed, nothing to recover",
                (void*)region);
    } else if (status == OS_EOK) {
        TRACE("SHMRegionUsageBeginRecovery: recovery started on region %p", (void*)region);
    }
    return status;
}

void
SHMRegionUsageEndRecovery(
    _In_ SHMRegionUsage_t* region)
{
    SpinlockAcquireIrq(&region->Lock);
    assert(region->Recovering && region->Faults != 0);
    // Order cache invalidation/flushing before marking recovery complete.
    dma_mb();
    region->Recovering = false;
    SpinlockReleaseIrq(&region->Lock);
    TRACE("SHMRegionUsageEndRecovery: recovery finished on region %p", (void*)region);
}
