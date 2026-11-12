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
#include <ddk/convert/grants.h>
#include <stdlib.h>
#include <string.h>

#include "netadapter-private.h"

// Bound the pairwise alias checks to 32 physical ranges per pool. This is
// separate from the packet slot count: one slot can cross several ranges.
#define ETHERNET_POOL_SEGMENTS 32

// A repeated registration must use the same buffer and layout. Check every
// field so the key cannot refer to different packet memory on a retry.
static bool
__SamePool(
    _In_ const struct ctt_netadapter_pool* left,
    _In_ const struct ctt_netadapter_pool* right)
{
    return left->direction == right->direction &&
           __SameSysSHMTransfer(&left->buffer, &right->buffer) &&
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
    SHMTransfer_t transfer;
    size_t        length;
    unsigned int  scope = GrantTokenScopeRead;
    oserr_t       status;

    status = __FromSysSHMTransfer(&description->buffer, &transfer);
    if (status != OS_EOK) {
        return status;
    }

    length = __SHMTransferLength(&transfer);
    if (description->direction == CTT_NETADAPTER_DIRECTION_RX) {
        scope |= GrantTokenScopeWrite;
    }

    if ((transfer.Token.Options.Scope & scope) != scope) {
        return OS_EPERMISSIONS;
    }

    if (description->direction != CTT_NETADAPTER_DIRECTION_TX &&
        description->direction != CTT_NETADAPTER_DIRECTION_RX) {
        return OS_EINVALPARAMS;
    }

    if (!description->slot_count || description->slot_count > ETHERNET_SLOTS) {
        return OS_EINVALPARAMS;
    }

    if (description->slot_size < ETHERNET_FRAME_SIZE) {
        return OS_EINVALPARAMS;
    }

    if (length > ETHERNET_POOL_BYTES) {
        return OS_EINVALPARAMS;
    }

    // Widen before multiplying the client-supplied fields. The resulting slot
    // array must fit within the selected token range, including every full slot.
    if ((uint64_t)description->slot_count * description->slot_size > length) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ValidateGeometry(
    _In_ EthernetDevice_t* device,
    _In_ EthernetPool_t*   candidate)
{
    SHMSGTable_t* table = &candidate->AliasGeometry;
    size_t        total = 0;

    // Packet slots must use separate physical memory, both within this pool
    // and across pools already in use.
    if (table->Count <= 0 || table->Count > ETHERNET_POOL_SEGMENTS || table->Entries == NULL) {
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
        for (uint32_t p = 0; p < device->Common->Session.PoolCount; ++p) {
            EthernetPool_t* pool = &device->Common->Session.Pools[p];
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
    _InOut_ EthernetPool_t* pool)
{
    oserr_t status;

    // Disable packet access before release. A failed release may have removed
    // part of the mapping, so the pool must stay unavailable until cleanup ends.
    pool->Mapped = false;

    status = SHMRelease(&pool->Memory);
    if (status != OS_EOK) {
        // Failed module teardown retries this call from its polling loop.
        WARNING_ONCE(ethernet_pool_release,
            "__ReleasePool: shared memory release failed (%u)", status);
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
    _In_    EthernetDevice_t*                 device,
    _In_    const struct ctt_netadapter_pool* description,
    _InOut_ EthernetPool_t*                   pool)
{
    SHMTransfer_t transfer;
    size_t        capacity;
    oserr_t       status;
    oserr_t       cleanupStatus;

    // A failed registration may have left resources in this slot without
    // increasing PoolCount. Finish releasing them before reusing the slot.
    status = __ReleasePool(pool);
    if (status != OS_EOK) {
        return status;
    }

    status = __FromSysSHMTransfer(&description->buffer, &transfer);
    if (status != OS_EOK) {
        return status;
    }

    pool->BufferOffset = transfer.RelativeOffset;

    status = SHMAttachToken(&transfer.Token, &pool->Memory);
    if (status != OS_EOK) {
        TRACE("__PreparePool: device %u token attach failed (%u)", device->Common->DeviceId, status);
        return status;
    }

    capacity = SHMBufferCapacity(&pool->Memory);
    if (capacity > ETHERNET_POOL_BYTES) {
        status = OS_EINVALPARAMS;
        goto error;
    }

    // RX memory must allow writes for received data and reads for destination
    // filtering. TX only needs read access. Padding uses a separate buffer.
    // Map from offset zero of the imported range. BufferOffset selects the
    // client's packet region later, and the alias check covers the whole range.
    status = SHMMap(
        &pool->Memory,
        0,
        capacity,
        description->direction == CTT_NETADAPTER_DIRECTION_RX
            ? SHM_ACCESS_READ | SHM_ACCESS_WRITE : SHM_ACCESS_READ
    );
    if (status != OS_EOK) {
        WARNING("__PreparePool: device %u mapping failed (%u)", device->Common->DeviceId, status);
        goto error;
    }

    // Use physical addresses to check whether SHM handles share memory. DMA
    // addresses may be translated or point to staging buffers, which would hide
    // an overlap. This table is only used for that check, never for descriptors.
    // A count of -1 asks SHMGetSGTable to allocate and retrieve the entire table.
    status = SHMGetSGTable(&pool->Memory, &pool->AliasGeometry, -1);
    if (status != OS_EOK) {
        WARNING("__PreparePool: device %u physical range query failed (%u)", device->Common->DeviceId, status);
        goto error;
    }

    status = __ValidateGeometry(device, pool);
    if (status != OS_EOK) {
        TRACE("__PreparePool: device %u invalid physical ranges (%u)", device->Common->DeviceId, status);
        goto error;
    }

    return OS_EOK;

error:
    cleanupStatus = __ReleasePool(pool);
    if (cleanupStatus != OS_EOK) {
        WARNING("ethernet pool rollback deferred: %u", cleanupStatus);
    }
    return status;
}

oserr_t
EthernetPoolRegister(
    _InOut_ EthernetDevice_t*                 device,
    _In_    uint64_t                          key,
    _In_    const struct ctt_netadapter_pool* description,
    _Out_   uint32_t*                         idOut)
{
    EthernetSession_t* session = &device->Common->Session;
    EthernetPool_t*    pool;
    oserr_t            status;

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
    if (!key || session->PoolCount == ETHERNET_POOLS || !EthernetIsIdle(device)) {
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
            pool->Description.buffer.token.export_id == description->buffer.token.export_id) {
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
    }

    // Pool IDs start at one. Never reuse an old ID or registration key within
    // the session, since delayed packets still carry those identifiers.
    *idOut = ++session->PoolCount;
    return OS_EOK;
}

oserr_t
EthernetPoolUnregister(
    _InOut_ EthernetDevice_t* device,
    _In_    uint32_t          id)
{
    EthernetPool_t* pool;

    if (!id || id > device->Common->Session.PoolCount) {
        return OS_EINVALPARAMS;
    }

    pool = &device->Common->Session.Pools[id - 1];
    if (!pool->Mapped) {
        // Retry any resources left after a failed release.
        return __ReleasePool(pool);
    }

    // Packets and their completion records must be finished before we release
    // the memory they refer to.
    if (!EthernetIsIdle(device)) {
        return OS_EBUSY;
    }

    return __ReleasePool(pool);
}

oserr_t
EthernetPoolsDetach(
    _InOut_ EthernetDevice_t* device)
{
    // Include failed registrations that left resources outside PoolCount.
    // All mappings and SHM attachments must be released before clearing the session.
    for (uint32_t i = 0; i < ETHERNET_POOLS; i++) {
        oserr_t status = __ReleasePool(&device->Common->Session.Pools[i]);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

void*
EthernetPacketBytes(
    _In_ EthernetSlot_t* slot)
{
    // The caller holds the adapter lock and the slot has been accepted.
    // Its address stays valid until successful pool release.
    return (char*)SHMBuffer(&slot->Pool->Memory)
        + slot->Pool->BufferOffset
        + (size_t)slot->Packet.id.slot_id * slot->Pool->Description.slot_size
        + slot->Packet.data_offset;
}

uint32_t
EthernetPacketLength(
    _In_ const EthernetSlot_t* slot)
{
    return slot->Packet.length;
}

bool
EthernetPacketIsReceive(
    _In_ const EthernetSlot_t* slot)
{
    return slot->Pool->Description.direction == CTT_NETADAPTER_DIRECTION_RX;
}
