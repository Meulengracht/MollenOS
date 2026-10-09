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

#include "virtio-net.h"
#include <stdlib.h>
#include <string.h>

// A registration key keeps the same meaning for the whole time its pool is
// attached. Matching every layout field prevents a retry from silently
// redirecting that key to different packet memory.
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
    // Earlier checks ensure these range ends do not wrap around the address
    // space, which could otherwise hide memory that is actually shared.
    return left->Address < right->Address + right->Length &&
           right->Address < left->Address + left->Length;
}

// Reject layouts that cannot be represented by this adapter before any shared
// memory is attached, so invalid requests do not acquire resources.
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
    SHMSGTable_t* table = &candidate->ScatterGather;
    size_t        total = 0;

    // The device needs one bounded, non-aliasing view of this memory so its
    // packet slots cannot overlap one another or any pool already in use.
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
        
        // Check the full mapped ranges, not just the bytes assigned to slots:
        // even unused bytes must not overlap another pool's device-visible memory.
        for (uint32_t p = 0; p < device->Session.PoolCount; ++p) {
            VirtioNetPool_t* pool = &device->Session.Pools[p];
            if (!pool->Mapped) {
                continue;
            }
            
            for (int j = 0; j < pool->ScatterGather.Count; ++j) {
                if (__Overlaps(segment, &pool->ScatterGather.Entries[j])) {
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

// Prepare the mapping before it becomes visible as a registered pool. If any
// check fails, release everything acquired here so registration stays all-or-none.
static oserr_t
__PreparePool(
    _In_    VirtioNetDevice_t*                device,
    _In_    const struct ctt_netadapter_pool* description,
    _InOut_ VirtioNetPool_t*                  pool)
{
    size_t  capacity;
    oserr_t status;

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

    // RX packets must be readable by software for destination filtering, while
    // the device also needs to write received data. TX data is only read by the
    // device, and any padding is kept outside the caller's buffer.
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

    status = SHMGetSGTable(&pool->Memory, &pool->ScatterGather, -1);
    if (status != OS_EOK) {
        goto error;
    }

    status = __ValidateGeometry(device, pool);
    if (status != OS_EOK) {
        goto error;
    }
    return OS_EOK;

error:
    // A rejected pool must leave no mapping behind, so the caller can correct
    // the request and try again without a half-prepared resource.
    OSHandleDestroy(&pool->Memory);
    free(pool->ScatterGather.Entries);
    memset(pool, 0, sizeof(*pool));
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

    // A repeated request is safe only when it still describes the same live
    // pool. This lets callers retry registration without changing its meaning.
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

    // Pool layouts affect packet slots and their completion state. Changing
    // them while work is active could make a packet refer to different memory.
    if (!key || session->PoolCount == VIRTIO_NET_POOLS || !VirtioNetIsIdle(device)) {
        return OS_EBUSY;
    }

    status = __ValidateDescription(description);
    if (status != OS_EOK) {
        return status;
    }

    // Each direction has one pool, and a shared buffer cannot be registered a
    // second time under another identity. This keeps ownership unambiguous.
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

    // No packet may still depend on a slot, and all reported completions must
    // be acknowledged before the session's pool layout can safely change.
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
        // Repeated cleanup is harmless once this pool has already been detached.
        return OS_EOK;
    }

    // Keep the mapping alive until packets and their completion records no
    // longer refer to it; releasing it earlier could invalidate active work.
    if (!VirtioNetIsIdle(device)) {
        return OS_EBUSY;
    }

    OSHandleDestroy(&pool->Memory);
    free(pool->ScatterGather.Entries);
    pool->ScatterGather = (SHMSGTable_t){0};
    pool->Mapped = false;
    return OS_EOK;
}

void
VirtioNetPoolsDetach(
    _InOut_ VirtioNetDevice_t* device)
{
    // Only release these mappings after reset or close has confirmed that the
    // device can no longer access them. Otherwise it could write into memory
    // that the system has already given to another owner.
    for (uint32_t i = 0; i < device->Session.PoolCount; ++i) {
        VirtioNetPool_t* pool = &device->Session.Pools[i];
        if (!pool->Mapped) {
            continue;
        }

        OSHandleDestroy(&pool->Memory);
        free(pool->ScatterGather.Entries);
        pool->ScatterGather = (SHMSGTable_t){0};
        pool->Mapped = false;
    }
}

void*
VirtioNetPacketBytes(
    _In_ VirtioNetSlot_t* slot)
{
    // Software RX filtering needs to inspect the packet while its slot is still
    // owned by the receive path, so return a temporary address into that pool.
    return (char*)SHMBuffer(&slot->Pool->Memory) 
        + slot->Pool->Description.region_offset
        + (size_t)slot->Packet.id.slot_id * slot->Pool->Description.slot_size
        + slot->Packet.data_offset;
}
