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
 * Session pool registration and physical alias validation.
 * 
 */

#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#include "virtio-net.h"

// A repeated registration must use the same buffer and layout. Check every
// field so the key cannot refer to different packet memory on a retry.
static bool
__SamePool(
    _In_ const struct ctt_netadapter_pool* left,
    _In_ const struct ctt_netadapter_pool* right)
{
    return left->direction == right->direction && left->buffer_handle == right->buffer_handle &&
           left->region_offset == right->region_offset && left->region_size == right->region_size &&
           left->slot_size == right->slot_size && left->slot_count == right->slot_count;
}

// Different shared-memory handles can still refer to the same physical bytes.
// Treating those bytes as separate pools could let one packet overwrite another.
static bool
__Overlaps(
    _In_ const SHMSG_t* left,
    _In_ const SHMSG_t* right)
{
    // Both range ends must have been checked for overflow before calling this.
    // Otherwise, an overlapping range could appear to end before the other starts.
    return left->Address < right->Address + right->Length &&
           right->Address < left->Address + left->Length;
}

// Check the adapter's layout limits before attaching shared memory.
static oserr_t
__ValidateDescription(
    _In_ const struct ctt_netadapter_pool* description)
{
    if (description->direction != CTT_NETADAPTER_DIRECTION_TX &&
        description->direction != CTT_NETADAPTER_DIRECTION_RX) {
        return OS_EINVALPARAMS;
    }
    if (!description->slot_count || description->slot_count > VIRTIO_NET_SLOTS) {
        return OS_EINVALPARAMS;
    }
    if (description->slot_size < VIRTIO_NET_FRAME_SIZE) {
        return OS_EINVALPARAMS;
    }
    if (description->region_size > VIRTIO_NET_POOL_BYTES ||
        description->region_offset > VIRTIO_NET_POOL_BYTES) {
        return OS_EINVALPARAMS;
    }
    if ((uint64_t)description->slot_count * description->slot_size > description->region_size) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ValidateGeometry(
    _In_ VirtioNetDevice_t* device,
    _In_ VirtioNetPool_t*   candidate)
{
    SHMSGTable_t* table = &candidate->AliasGeometry;
    size_t        total = 0;

    // Packet slots must use separate physical memory, both within this pool
    // and across pools already in use.
    if (table->Count <= 0 || table->Count > 32 || table->Entries == NULL) {
        return OS_ENOTSUPPORTED;
    }
    
    for (int i = 0; i < table->Count; ++i) {
        SHMSG_t* segment = &table->Entries[i];
        if (!segment->Length || segment->Address > UINTPTR_MAX - segment->Length ||
            segment->Length > SIZE_MAX - total) {
            return OS_EINVALPARAMS;
        }
        
        total += segment->Length;
        for (int j = 0; j < i; ++j) {
            if (__Overlaps(segment, &table->Entries[j])) {
                return OS_EINVALPARAMS;
            }
        }
        
        // Check the whole buffer. Bytes outside the packet slots must also be
        // separate from every registered pool's memory.
        for (uint32_t p = 0; p < device->Session.PoolCount; ++p) {
            VirtioNetPool_t* pool = &device->Session.Pools[p];
            if (!pool->Mapped) {
                continue;
            }
            
            for (int j = 0; j < pool->AliasGeometry.Count; ++j) {
                if (__Overlaps(segment, &pool->AliasGeometry.Entries[j])) {
                    return OS_EINVALPARAMS;
                }
            }
        }
    }

    if (total < SHMBufferCapacity(&candidate->Memory)) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ReleasePool(
    _InOut_ VirtioNetPool_t* pool)
{
    oserr_t status;

    // DMA must have ended for every slot after completion or reset. If release
    // fails, keep the pool so later cleanup can retry the remaining resources.
    pool->Mapped = false;
    for (uint32_t i = 0; i < VIRTIO_NET_SLOTS; i++) {
        status = DMAUnmap(&pool->Slots[i].Mapping);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = SHMRelease(&pool->Memory);
    if (status != OS_EOK) {
        return status;
    }
    
    free(pool->AliasGeometry.Entries);
    pool->AliasGeometry = (SHMSGTable_t){0};
    return OS_EOK;
}

// Prepare mappings before registering the pool. If setup and cleanup both fail,
// keep the remaining resources in this pool slot for another cleanup attempt.
static oserr_t
__PreparePool(
    _In_    VirtioNetDevice_t*                device,
    _In_    const struct ctt_netadapter_pool* description,
    _InOut_ VirtioNetPool_t*                  pool)
{
    size_t  capacity;
    oserr_t status;
    oserr_t cleanupStatus;

    // A failed registration may have left resources in this slot without
    // increasing PoolCount. Finish releasing them before reusing the slot.
    status = __ReleasePool(pool);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMAttach(description->buffer_handle, &pool->Memory);
    if (status != OS_EOK) {
        return status;
    }

    capacity = SHMBufferCapacity(&pool->Memory);
    if (capacity > VIRTIO_NET_POOL_BYTES || description->region_offset > capacity ||
        description->region_size > capacity - description->region_offset) {
        status = OS_EINVALPARAMS;
        goto error;
    }

    // RX memory must allow writes for received data and reads for destination
    // filtering. TX only needs read access. Padding uses a separate buffer.
    status = SHMMap(
        &pool->Memory,
        0,
        capacity,
        description->direction == CTT_NETADAPTER_DIRECTION_RX
            ? SHM_ACCESS_READ | SHM_ACCESS_WRITE : SHM_ACCESS_READ
    );
    if (status != OS_EOK) {
        goto error;
    }

    // Check physical addresses for overlapping SHM buffers. DMA addresses can
    // be translated or refer to staging buffers and hide the overlap.
    // Use this table only for the check. Descriptors need DMA addresses.
    status = SHMGetSGTable(&pool->Memory, &pool->AliasGeometry, -1);
    if (status != OS_EOK) {
        goto error;
    }

    status = __ValidateGeometry(device, pool);
    if (status != OS_EOK) {
        goto error;
    }

    // Map each slot separately so DMA can begin and end for each packet.
    // These mappings stay in place, so packet transfers need no new allocation.
    for (uint32_t i = 0; i < description->slot_count; i++) {
        status = DMAMapSHM(
            device->Transport.DmaContext,
            &pool->Memory,
            description->region_offset + (size_t)i * description->slot_size,
            description->slot_size,
            description->direction == CTT_NETADAPTER_DIRECTION_RX ? DMAFromDevice : DMAToDevice,
            NULL,
            &pool->Slots[i].Mapping
        );
        if (status != OS_EOK) {
            goto error;
        }
    }
    return OS_EOK;

error:
    cleanupStatus = __ReleasePool(pool);
    if (cleanupStatus != OS_EOK) {
        WARNING("virtio-net pool rollback deferred: %u", cleanupStatus);
    }
    return status;
}

oserr_t
VirtioNetPoolRegister(
    _InOut_ VirtioNetDevice_t*                device,
    _In_    uint64_t                          key,
    _In_    const struct ctt_netadapter_pool* description,
    _Out_   uint32_t*                         idOut)
{
    VirtioNetSession_t* session = &device->Session;
    VirtioNetPool_t*    pool;
    oserr_t             status;

    *idOut = 0;

    // Allow registration retries only while the pool is mapped and its
    // description is unchanged.
    for (uint32_t i = 0; i < session->PoolCount; ++i) {
        pool = &session->Pools[i];
        if (pool->Registration == key) {
            if (!pool->Mapped || !__SamePool(&pool->Description, description)) {
                return OS_EPROTOCOL;
            }
            *idOut = i + 1;
            return OS_EOK;
        }
    }

    // The session must be idle before changing its pools. Pending packets and
    // completions still refer to slots in the current layout.
    if (!key || session->PoolCount == VIRTIO_NET_POOLS || !VirtioNetIsIdle(device)) {
        return OS_EBUSY;
    }

    status = __ValidateDescription(description);
    if (status != OS_EOK) {
        return status;
    }

    // Allow one pool per direction. A shared buffer already in use cannot be
    // registered again under another key.
    for (uint32_t i = 0; i < session->PoolCount; ++i) {
        pool = &session->Pools[i];
        if (!pool->Mapped) {
            continue;
        }
        if (pool->Description.direction == description->direction ||
            pool->Description.buffer_handle == description->buffer_handle) {
            return OS_EINVALPARAMS;
        }
    }

    pool = &session->Pools[session->PoolCount];
    status = __PreparePool(device, description, pool);
    if (status != OS_EOK) {
        return status;
    }

    pool->Description = *description;
    pool->Registration = key;
    pool->Mapped = true;

    for (uint32_t i = 0; i < description->slot_count; ++i) {
        pool->Slots[i].Pool = pool;
        pool->Slots[i].MetadataOffset =
                (session->PoolCount * VIRTIO_NET_SLOTS + i) * VIRTIO_NET_METADATA_STRIDE;
    }

    *idOut = ++session->PoolCount;
    return OS_EOK;
}

bool
VirtioNetIsIdle(
    _In_ const VirtioNetDevice_t* device)
{
    const VirtioNetSession_t* session = &device->Session;

    // Pool changes require no outstanding packets and all completions acknowledged.
    return (session->State == VIRTIO_NET_OPENED || session->State == VIRTIO_NET_STOPPED) &&
           !session->Outstanding[0] && !session->Outstanding[1] &&
           session->Progress.highest_completion_sequence ==
                   session->Progress.retired_completion_sequence;
}

oserr_t
VirtioNetPoolUnregister(
    _InOut_ VirtioNetDevice_t* device,
    _In_    uint32_t           id)
{
    VirtioNetPool_t* pool;

    if (!id || id > device->Session.PoolCount) {
        return OS_EINVALPARAMS;
    }

    pool = &device->Session.Pools[id - 1];
    if (!pool->Mapped) {
        // Retry any resources left after a failed release.
        return __ReleasePool(pool);
    }

    // Packets and their completion records must be finished before we release
    // the memory they refer to.
    if (!VirtioNetIsIdle(device)) {
        return OS_EBUSY;
    }

    return __ReleasePool(pool);
}

oserr_t
VirtioNetPoolsDetach(
    _InOut_ VirtioNetDevice_t* device)
{
    // Include failed registrations that left resources outside PoolCount.
    // All mappings and SHM attachments must be released before clearing the session.
    for (uint32_t i = 0; i < VIRTIO_NET_POOLS; i++) {
        oserr_t status = __ReleasePool(&device->Session.Pools[i]);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

void*
VirtioNetPacketBytes(
    _In_ VirtioNetSlot_t* slot)
{
    // DMA must have ended before RX filtering reads the packet. The returned
    // address is valid only while the pool remains attached.
    return (char*)SHMBuffer(&slot->Pool->Memory) 
        + slot->Pool->Description.region_offset
        + (size_t)slot->Packet.id.slot_id * slot->Pool->Description.slot_size
        + slot->Packet.data_offset;
}
