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
 * Prepares, caches, revokes, and stops fixed-region state for
 * SHMDeviceMappingRegions DMA mappings.
 */

//#define __TRACE

#include "private.h"
#include <arch/utils.h>
#include <assert.h>
#include <ddk/barrier.h>
#include <debug.h>
#include <heap.h>
#include <string.h>

// Walk the mapping's DMA segments and verify every slice covered by this
// region. Even when the whole mapping meets the device's alignment rules, a
// boundary between two regions inside a segment can still leave one region's
// start address or length unaligned.
static oserr_t
__CheckRegionSegments(
    _In_ SHMDeviceMapping_t*              mapping,
    _In_ SHMDeviceRegion_t*               region,
    _In_ const struct DMAMapRequirements* requirements)
{
    uint32_t                 count;
    const struct DMASegment* segments = DMAAddressMappingSegments(mapping->AddressMapping, &count);
    size_t                   skip = region->Offset;
    size_t                   remaining = region->Binding.Length;

    for (uint32_t i = 0; i < count && remaining != 0; i++) {
        uint64_t length;
        uint64_t address;

        if (skip >= segments[i].Length) {
            skip -= segments[i].Length;
            continue;
        }
        address = segments[i].Address + skip;
        length = segments[i].Length - skip;
        if (length > remaining) {
            length = remaining;
        }
        if (requirements != NULL &&
            ((requirements->AddressAlignment != 0 && address % requirements->AddressAlignment != 0) ||
             (requirements->LengthAlignment != 0 && length % requirements->LengthAlignment != 0))) {
            WARNING("__CheckRegionSegments: address 0x%llx length %llu does not meet the alignment limits",
                (unsigned long long)address, (unsigned long long)length);
            return OS_ENOTSUPPORTED;
        }
        remaining -= length;
        skip = 0;
    }
    return remaining == 0 ? OS_EOK : OS_EINVALPARAMS;
}

// Populate region metadata from the buffer's registered region bindings.
// bindings[i].Offset is measured from the start of the SHM buffer, while
// region->Offset is relative to this mapping. Maps each region to its starting
// physical RAM extent (used for CPU cache maintenance) and checks DMA limits.
static oserr_t
__BuildRegions(
    _In_ SHMDeviceMapping_t*              mapping,
    _In_ const SHMRegionBinding_t*        bindings,
    _In_ unsigned int                     count,
    _In_ SHMDeviceRegions_t*              regions,
    _In_ const struct DMAMapRequirements* requirements)
{
    unsigned int extent = 0;
    size_t       extentStart = 0;
    oserr_t      status;

    regions->Count = count;
    for (unsigned int i = 0; i < count; i++) {
        SHMDeviceRegion_t* region = &regions->Entries[i];

        region->Binding = bindings[i];
        region->Offset = bindings[i].Offset - mapping->Offset;
        while (extent < (unsigned int)mapping->ExtentCount &&
               region->Offset - extentStart >= mapping->Extents[extent].Length) {
            extentStart += mapping->Extents[extent++].Length;
        }
        if (extent == (unsigned int)mapping->ExtentCount) {
            ERROR("__BuildRegions: region %u at offset %u is outside the mapping's extents",
                i, (unsigned int)region->Offset);
            return OS_EINVALPARAMS;
        }
        region->FirstExtent = extent;
        region->ExtentOffset = region->Offset - extentStart;
        status = __CheckRegionSegments(mapping, region, requirements);
        if (status != OS_EOK) {
            WARNING("__BuildRegions: region %u does not fit the device requirements: %u", i, status);
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
SHMDeviceRegionsPrepare(
    _In_ SHMDeviceMapping_t*              mapping,
    _In_ const struct DMAMapRequirements* requirements)
{
    SHMRegionBinding_t* bindings;
    SHMDeviceRegions_t* regions;
    unsigned int        count;
    oserr_t             status;

    // Query how many registered regions span this mapped range first so we can
    // allocate the temporary bindings list and the final region table once.
    status = SHMRegionDescribeRange(mapping->Access.Buffer, mapping->Offset,
        mapping->Length, NULL, 0, &count);
    if (status != OS_EOK) {
        WARNING("SHMDeviceRegionsPrepare: cannot count the regions of mapping %p: %u", mapping, status);
        return status;
    }
    _Static_assert(SHM_REGION_LIMIT <= (SIZE_MAX - sizeof(*regions)) / sizeof(regions->Entries[0]),
        "Registered regions fit the mapping metadata allocation");
    bindings = kmalloc(count * sizeof(*bindings));
    if (bindings == NULL) {
        ERROR("SHMDeviceRegionsPrepare: failed to allocate %u region bindings", count);
        return OS_EOOM;
    }
    regions = kmalloc(sizeof(*regions) + count * sizeof(regions->Entries[0]));
    if (regions == NULL) {
        ERROR("SHMDeviceRegionsPrepare: failed to allocate %u region entries", count);
        kfree(bindings);
        return OS_EOOM;
    }
    memset(regions, 0, sizeof(*regions) + count * sizeof(regions->Entries[0]));
    status = SHMRegionDescribeRange(mapping->Access.Buffer, mapping->Offset,
        mapping->Length, bindings, count, &count);
    if (status != OS_EOK) {
        ERROR("SHMDeviceRegionsPrepare: cannot describe the regions of mapping %p: %u", mapping, status);
        goto fail;
    }
    status = __BuildRegions(mapping, bindings, count, regions, requirements);
    if (status != OS_EOK) {
        goto fail;
    }
    mapping->Regions = regions;
    kfree(bindings);
    TRACE("SHMDeviceRegionsPrepare: mapping %p has %u regions", mapping, count);
    return OS_EOK;
fail:
    kfree(regions);
    kfree(bindings);
    return status;
}

// Pick the CPU cache operation for one region transfer. Uncached pages and
// cache-coherent devices already match RAM, so they return NULL. Starting a
// device read (DMAToDevice) cleans dirty CPU lines to RAM, while finishing one
// needs nothing because the device wrote nothing. Starting a device write or
// bidirectional transfer cleans and invalidates so dirty CPU lines flush out
// and stale lines are dropped. Finishing one invalidates any CPU lines loaded
// during DMA so the CPU reads fresh bytes from RAM.
static void (*__SelectCacheOperation(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ enum DMADirection   direction,
    _In_ bool                finish))(uintptr_t, size_t)
{
    if (!mapping->Cached || mapping->Context->CachePolicy == SHMDeviceCacheCoherent ||
        (finish && direction == DMAToDevice)) {
        return NULL;
    }
    if (finish) {
        return CpuDataCacheInvalidate;
    }
    if (direction == DMAToDevice) {
        return CpuDataCacheClean;
    }
    return CpuDataCacheCleanInvalidate;
}

void
SHMDeviceRegionsCache(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ SHMDeviceRegion_t*  region,
    _In_ enum DMADirection   direction,
    _In_ bool               finish)
{
    size_t remaining = region->Binding.Length;
    size_t skip = region->ExtentOffset;
    void (*maintain)(uintptr_t, size_t);

    // Order earlier CPU stores before cache maintenance, and order finished
    // cache maintenance before device register writes or CPU reads.
    dma_mb();
    maintain = __SelectCacheOperation(mapping, direction, finish);
    if (maintain == NULL) {
        return;
    }
    for (unsigned int i = region->FirstExtent; remaining != 0; i++) {
        const SHMSG_t* extent;
        size_t        length;

        assert(i < (unsigned int)mapping->ExtentCount);
        extent = &mapping->Extents[i];
        length = extent->Length - skip;
        if (length > remaining) {
            length = remaining;
        }
        maintain(extent->Address + skip, length);
        remaining -= length;
        skip = 0;
    }
    dma_mb();
}

oserr_t
SHMDeviceRegionsRevoke(
    _In_ SHMDeviceMapping_t* mapping)
{
    oserr_t firstError = OS_EOK;

    // Mark every region faulted so future CPU or device accesses fail with
    // OS_EDEVFAULT. Keep going across all regions even if one fails.
    MutexLock(&mapping->Context->MappingLock);
    for (unsigned int i = 0; i < mapping->Regions->Count; i++) {
        SHMDeviceRegion_t* region = &mapping->Regions->Entries[i];
        oserr_t status = SHMRegionUsageFault(region->Binding.Usage, &region->Fault);

        if (status != OS_EOK) {
            ERROR("SHMDeviceRegionsRevoke: cannot fault region %u of mapping %p: %u", i, mapping, status);
            if (firstError == OS_EOK) {
                firstError = status;
            }
        }
    }
    MutexUnlock(&mapping->Context->MappingLock);
    return firstError;
}

// The recovery group has blocked new syscalls and confirmed hardware stopped.
// Move all live token accesses to stopped before running recovery cache work,
// so SHMRegionUsageBeginRecovery sees no active transfer left from this mapping.
static oserr_t
__StopTokens(
    _In_ SHMDeviceMapping_t* mapping)
{
    for (SHMDeviceRegionSession_t* session = mapping->Regions->Sessions; session != NULL; session = session->Next) {
        for (unsigned int i = 0; i < session->Capacity; i++) {
            SHMDeviceRegionToken_t* token = &session->Tokens[i];
            oserr_t status = OS_EOK;

            SpinlockAcquireIrq(&session->Lock);
            if (token->Access.State == SHMRegionAccessLive) {
                status = SHMRegionUsageStop(&token->Access, token->Access.Generation);
                if (status == OS_EOK) {
                    token->Phase = SHMDeviceRegionStopped;
                }
            }
            SpinlockReleaseIrq(&session->Lock);
            if (status != OS_EOK) {
                ERROR("__StopTokens: cannot stop token %u of session %p: %u", i, session, status);
                return status;
            }
        }
    }
    return OS_EOK;
}

// Finish every stopped token in sessions attached to this region. Callers that
// later call End for these transfers receive OS_EDEVFAULT because the device
// was stopped while they were running.
static oserr_t
__EndStoppedTokens(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ SHMDeviceRegion_t*  region)
{
    for (SHMDeviceRegionSession_t* session = mapping->Regions->Sessions; session != NULL; session = session->Next) {
        if (session->Region != region) {
            continue;
        }
        for (unsigned int i = 0; i < session->Capacity; i++) {
            SHMDeviceRegionToken_t* token = &session->Tokens[i];
            oserr_t status = OS_EOK;

            SpinlockAcquireIrq(&session->Lock);
            if (token->Phase == SHMDeviceRegionStopped) {
                status = SHMRegionUsageEnd(&token->Access, token->Access.Generation);
                if (status == OS_EOK) {
                    token->Phase = SHMDeviceRegionFinished;
                    token->Result = OS_EDEVFAULT;
                }
            }
            SpinlockReleaseIrq(&session->Lock);
            if (status != OS_EOK) {
                ERROR("__EndStoppedTokens: cannot end token %u of session %p: %u", i, session, status);
                return status;
            }
        }
    }
    return OS_EOK;
}

// Run post-stop cache cleanup for one region once hardware has stopped. Another
// device group or CPU session may still be reading the buffer, so writable
// mappings take brief recovery access before invalidating CPU caches.
static oserr_t
__FinishRegionCache(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ SHMDeviceRegion_t*  region)
{
    oserr_t status;

    if (region->CacheDone) {
        return OS_EOK;
    }
    if (mapping->Direction == DMAToDevice) {
        // A read-only mapping never lets the device modify RAM, so it needs no
        // cache invalidation and cannot disturb a shared reader.
        SHMDeviceRegionsCache(mapping, region, DMAToDevice, true);
        region->CacheDone = true;
        return OS_EOK;
    }
    status = SHMRegionUsageBeginRecovery(region->Binding.Usage);
    if (status != OS_EOK) {
        WARNING("__FinishRegionCache: cannot begin recovery for a region of mapping %p: %u", mapping, status);
        return status;
    }
    SHMDeviceRegionsCache(mapping, region, mapping->Direction, true);
    region->CacheDone = true;
    SHMRegionUsageEndRecovery(region->Binding.Usage);
    return OS_EOK;
}

oserr_t
SHMDeviceRegionsStop(
    _In_ SHMDeviceMapping_t* mapping)
{
    oserr_t status;
    oserr_t firstError = OS_EOK;

    // Step 1: mark every live token stopped so recovery sees no active transfer.
    status = __StopTokens(mapping);
    if (status != OS_EOK) {
        return status;
    }

    // Step 2: finish cache cleanup per region, then end its stopped tokens.
    for (unsigned int i = 0; i < mapping->Regions->Count; i++) {
        SHMDeviceRegion_t* region = &mapping->Regions->Entries[i];

        status = __FinishRegionCache(mapping, region);
        if (status != OS_EOK) {
            // Keep tokens in SHMDeviceRegionStopped until a retry finishes
            // cache maintenance for this region.
            if (firstError == OS_EOK) {
                firstError = status;
            }
            continue;
        }
        status = __EndStoppedTokens(mapping, region);
        if (status != OS_EOK && firstError == OS_EOK) {
            firstError = status;
        }
    }
    return firstError;
}

oserr_t
SHMDeviceRegionsClearFaults(
    _In_ SHMDeviceMapping_t* mapping)
{
    for (unsigned int i = 0; i < mapping->Regions->Count; i++) {
        oserr_t status = SHMRegionUsageClearFault(&mapping->Regions->Entries[i].Fault);

        if (status != OS_EOK) {
            ERROR("SHMDeviceRegionsClearFaults: cannot clear region %u of mapping %p: %u", i, mapping, status);
            return status;
        }
    }
    return OS_EOK;
}
