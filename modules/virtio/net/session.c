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

#include "virtio-net.h"
#include <string.h>

// Keep the client's two progress markers within work the device has actually
// accepted or completed, so neither side can retire records it still needs.
oserr_t
VirtioNetAcknowledge(
    _InOut_ VirtioNetDevice_t*            device,
    _In_ const struct ctt_netadapter_ack* ack)
{
    struct ctt_netadapter_progress* progress = &device->Session.Progress;
    
    // Both markers describe one acknowledgement. Rejecting both together avoids
    // leaving the client and device with different views of retired work.
    if (ack->through_batch_id > progress->consumed_batch_id ||
        ack->through_completion_sequence > progress->highest_completion_sequence) {
        return OS_EINVALPARAMS;
    }
    if (ack->through_batch_id > progress->retired_batch_id) {
        progress->retired_batch_id = ack->through_batch_id;
    }
    if (ack->through_completion_sequence > progress->retired_completion_sequence) {
        progress->retired_completion_sequence = ack->through_completion_sequence;
    }
    return OS_EOK;
}

// Turn a device-owned packet into one durable result and release the resources
// it was using. Keeping the result in the journal lets the client recover it
// even when the first notification cannot be delivered.
void
VirtioNetComplete(
        _InOut_ VirtioNetDevice_t* device,
        _InOut_ VirtioNetSlot_t*   slot,
        _In_ oserr_t               status,
        _In_ uint32_t              length)
{
    VirtioNetSession_t* session = &device->Session;
    if (!slot->Owned) {
        // A slot has only one terminal result; ignoring later callbacks protects
        // completion counts and packet statistics from being updated twice.
        return;
    }

    enum ctt_netadapter_direction direction = slot->Pool->Description.direction;
    uint64_t                      sequence = ++session->Progress.highest_completion_sequence;
    
    session->Journal[(sequence - 1) % VIRTIO_NET_JOURNAL] = 
        (struct ctt_netadapter_completion) {
            .completion_sequence = sequence,
            .direction = direction,
            .id = slot->Packet.id,
            .status = status == OS_EOK          ? CTT_NETADAPTER_COMPLETION_STATUS_SUCCESS
                      : status == OS_ECANCELLED ? CTT_NETADAPTER_COMPLETION_STATUS_CANCELLED
                                                : CTT_NETADAPTER_COMPLETION_STATUS_ERROR,
            .detail = status,
            .length = status == OS_EOK ? length : 0
        };
    
    slot->Owned = false;
    session->Outstanding[direction == CTT_NETADAPTER_DIRECTION_TX ? 0 : 1]--;
    if (status == OS_EOK) {
        if (direction == CTT_NETADAPTER_DIRECTION_TX) {
            session->Counters.tx_packets++;
            session->Counters.tx_bytes += length;
        } else {
            session->Counters.rx_packets++;
            session->Counters.rx_bytes += length;
        }
    } else if (status != OS_ECANCELLED) {
        if (direction == CTT_NETADAPTER_DIRECTION_TX) {
            session->Counters.tx_errors++;
        } else {
            session->Counters.rx_errors++;
        }
    }
}

// Notify the client about journaled results after its last known position.
// Results stay in the journal until acknowledged, so a failed notification can
// be recovered later without losing or changing the original result.
void
VirtioNetPushCompletions(
    _In_ VirtioNetDevice_t* device,
    _In_ uint64_t           after)
{
    VirtioNetSession_t* session = &device->Session;
    int                 status;
    
    while (after < session->Progress.highest_completion_sequence) {
        struct ctt_netadapter_completion records[VIRTIO_NET_BATCH];
        uint32_t                         count = 0;
        
        while (count < VIRTIO_NET_BATCH && after < session->Progress.highest_completion_sequence) {
            records[count++] = session->Journal[after++ % VIRTIO_NET_JOURNAL];
        }

        // A failed send only delays notice. The client can request these same
        // saved results later and acknowledge them after receiving them.
        status = ctt_netadapter_event_completions_single(
            VirtioNetServer(),
            session->Owner,
            &session->Identity,
            0,
            records,
            count,
            &session->Progress
        );
        if (status) {
            break;
        }
    }
}

// Decide whether a repeated batch contains the same request as the first one.
// Batch identifiers can be retried after a lost reply, but must never name new
// packet data on a retry.
static bool
__SamePacket(
    _In_ const struct ctt_netadapter_packet* left,
    _In_ const struct ctt_netadapter_packet* right)
{
    if (left->id.queue_id != right->id.queue_id || left->id.pool_id != right->id.pool_id ||
        left->id.slot_id != right->id.slot_id) {
        return false;
    }
    if (left->id.submission_sequence != right->id.submission_sequence ||
        left->data_offset != right->data_offset || left->length != right->length) {
        return false;
    }
    return left->flags == right->flags;
}

// Resolve a packet's pool and slot only when both belong to the requested
// direction. Keeping this check separate prevents an invalid identifier from
// being used to access session-owned memory.
static oserr_t
__FindPacketSlot(
    _In_  VirtioNetSession_t*                 session,
    _In_  enum ctt_netadapter_direction       direction,
    _In_  const struct ctt_netadapter_packet* packet,
    _Out_ VirtioNetPool_t**                   poolOut,
    _Out_ VirtioNetSlot_t**                   slotOut)
{
    VirtioNetPool_t* pool;

    if (!packet->id.pool_id || packet->id.pool_id > session->PoolCount) {
        return OS_EINVALPARAMS;
    }
    
    pool = &session->Pools[packet->id.pool_id - 1];
    if (!pool->Mapped || pool->Description.direction != direction) {
        return OS_EINVALPARAMS;
    }
    if (packet->id.queue_id || packet->id.slot_id >= pool->Description.slot_count) {
        return OS_EINVALPARAMS;
    }

    *poolOut = pool;
    *slotOut = &pool->Slots[packet->id.slot_id];
    return OS_EOK;
}

// Keep packet data within its registered slot and enforce the size each
// direction can safely handle.
static bool
__PacketSizeValid(
        _In_ const VirtioNetPool_t*              pool,
        _In_ enum ctt_netadapter_direction       direction,
        _In_ const struct ctt_netadapter_packet* packet)
{
    if (packet->flags || packet->data_offset > pool->Description.slot_size) {
        return false;
    }
    if (packet->length > pool->Description.slot_size - packet->data_offset) {
        return false;
    }
    if (direction == CTT_NETADAPTER_DIRECTION_TX &&
        (packet->length < 14 || packet->length > VIRTIO_NET_FRAME_SIZE)) {
        return false;
    }
    if (direction == CTT_NETADAPTER_DIRECTION_RX && packet->length < VIRTIO_NET_FRAME_SIZE) {
        return false;
    }
    return true;
}

// Admit one packet only when its pool, slot, sequence, size, and session state
// make it safe to give the packet to the device. Reserving completion space
// before queueing ensures every accepted packet can later report its result.
static oserr_t
__AdmitPacket(
    _InOut_ VirtioNetDevice_t*                  device,
    _In_    enum ctt_netadapter_direction       direction,
    _In_    const struct ctt_netadapter_packet* packet)
{
    VirtioNetSession_t* session = &device->Session;
    VirtioNetPool_t*   pool;
    VirtioNetSlot_t*   slot;
    oserr_t            status;
    unsigned           index;
    uint32_t           outstanding;

    status = __FindPacketSlot(session, direction, packet, &pool, &slot);
    if (status != OS_EOK) {
        return status;
    }
    if (!packet->id.submission_sequence || packet->id.submission_sequence <= slot->Sequence) {
        return OS_EINVALPARAMS;
    }

    // A rejected descriptor still uses its sequence, preventing an old request
    // from being replayed later as if it were new.
    slot->Sequence = packet->id.submission_sequence;
    if (slot->Owned) {
        return OS_EBUSY;
    }
    
    if (!__PacketSizeValid(pool, direction, packet)) {
        return OS_EINVALPARAMS;
    }
    if (session->Faulted) {
        return OS_EDEVFAULT;
    }
    
    if (direction == CTT_NETADAPTER_DIRECTION_TX &&
        device->Link.status != CTT_NETADAPTER_LINK_STATUS_UP) {
        return OS_ENOTCONNECTED;
    }
    
    index = direction == CTT_NETADAPTER_DIRECTION_TX ? 0 : 1;
    outstanding = session->Outstanding[0] + session->Outstanding[1];
    if (session->Outstanding[index] == VIRTIO_NET_SLOTS ||
        session->Progress.highest_completion_sequence -
                        session->Progress.retired_completion_sequence + outstanding >=
                VIRTIO_NET_JOURNAL) {
        return OS_EBUSY;
    }
    if (session->Progress.highest_completion_sequence >= UINT64_MAX - outstanding) {
        return OS_EOVERFLOW;
    }
    
    slot->Packet = *packet;
    
    // Reserve space for a future result before the device can see the packet.
    // Once queued, it may be too late to withdraw the packet safely.
    slot->Owned = true;
    session->Outstanding[index]++;
    
    status = VirtioNetQueuePacket(device, slot);
    if (status == OS_EINPROGRESS) {
        VirtioNetFault(device, status);
        return OS_EOK;
    }
    if (status != OS_EOK) {
        slot->Owned = false;
        session->Outstanding[index]--;
        if (status == OS_EBUSY && index == 0) {
            session->Counters.tx_queue_full++;
        }
    }
    return status;
}

// Verify whether a batch is a valid retry or the next new batch. A retry is
// accepted only when every identifying detail matches the saved request.
static oserr_t
__ValidateBatch(
    _In_  VirtioNetSession_t*                 session,
    _In_  VirtioNetBatch_t*                   batch,
    _In_  uint64_t                            run,
    _In_  uint64_t                            batchId,
    _In_  const struct ctt_netadapter_packet* packets,
    _In_  uint32_t                            count,
    _In_  enum ctt_netadapter_direction       direction,
    _Out_ bool*                               fresh)
{
    uint32_t i;

    *fresh = false;

    if (!run || run != session->Run || !batchId) {
        return OS_ENOENT;
    }
    if (batchId <= session->Progress.retired_batch_id) {
        return OS_ENOENT;
    }
    if (!count || count > VIRTIO_NET_BATCH) {
        return OS_EINVALPARAMS;
    }

    if (batchId <= session->Progress.consumed_batch_id) {
        if (batch->Id != batchId || batch->Run != run) {
            return OS_EPROTOCOL;
        }
        if (batch->Direction != direction || batch->Count != count) {
            return OS_EPROTOCOL;
        }
        for (i = 0; i < count; ++i) {
            if (!__SamePacket(&batch->Packets[i], &packets[i])) {
                return OS_EPROTOCOL;
            }
        }
        return OS_EOK;
    }

    if (session->State != VIRTIO_NET_PREPARED && session->State != VIRTIO_NET_RUNNING) {
        return OS_EBUSY;
    }
    if (direction == CTT_NETADAPTER_DIRECTION_TX && session->State != VIRTIO_NET_RUNNING) {
        return OS_EBUSY;
    }
    if (batchId != session->Progress.consumed_batch_id + 1) {
        return OS_EBUSY;
    }
    if (batchId - session->Progress.retired_batch_id > VIRTIO_NET_WINDOW) {
        return OS_EBUSY;
    }
    *fresh = true;
    return OS_EOK;
}

// Save a new batch before admission so the original results remain available
// if the caller needs to retry after losing the response.
static void
__AdmitBatch(
    _InOut_ VirtioNetDevice_t*               device,
    _InOut_ VirtioNetBatch_t*                batch,
    _In_ uint64_t                            run,
    _In_ uint64_t                            batchId,
    _In_ const struct ctt_netadapter_packet* packets,
    _In_ uint32_t                            count,
    _In_ enum ctt_netadapter_direction       direction)
{
    VirtioNetSession_t* session = &device->Session;
    uint32_t            i;

    *batch = (VirtioNetBatch_t) {
        .Id = batchId,
        .Run = run,
        .Direction = direction,
        .Count = count
    };
    memcpy(batch->Packets, packets, count * sizeof(*packets));
    
    // The batch is consumed even if individual packets are rejected. Its
    // saved answers let the caller retry delivery without changing history.
    for (i = 0; i < count; ++i) {
        batch->Admissions[i].id = batch->Packets[i].id;
        batch->Admissions[i].status = __AdmitPacket(
            device,
            direction,
            &batch->Packets[i]
        );
    }
    session->Progress.consumed_batch_id = batchId;
}

// Process a batch once and save its per-packet admission results. Saving the
// request and its results makes retries safe: a repeated batch gets the same
// answer instead of queueing its packets a second time.
void
VirtioNetSubmit(
    _InOut_ VirtioNetDevice_t*                  device,
    _In_    uint64_t                            run,
    _In_    uint64_t                            batchId,
    _In_    const struct ctt_netadapter_ack*    ack,
    _In_    const struct ctt_netadapter_packet* packets,
    _In_    uint32_t                            count,
    _In_    enum ctt_netadapter_direction       direction)
{
    VirtioNetSession_t* session = &device->Session;
    oserr_t             status;
    VirtioNetBatch_t*   batch;
    bool                fresh = false;
    
    batch = &session->Batches[batchId ? (batchId - 1) % VIRTIO_NET_WINDOW : 0];
    
    status = VirtioNetAcknowledge(device, ack);
    if (status != OS_EOK) {
        goto reply;
    }

    status = __ValidateBatch(
        session,
        batch,
        run,
        batchId,
        packets,
        count,
        direction,
        &fresh
    );

    if (fresh) {
        __AdmitBatch(device, batch, run, batchId, packets, count, direction);
    }

reply:
    ctt_netadapter_event_batch_admitted_single(
        VirtioNetServer(),
        session->Owner,
        &session->Identity,
        run,
        batchId,
        status,
        status == OS_EOK ? batch->Admissions : NULL,
        status == OS_EOK ? count : 0,
        &session->Progress
    );
}

// Stop accepting work at the caller's batch boundary, reset the queues, and
// mark the last completion produced by that reset. Repeating the same stop is
// allowed so a lost reply does not make the caller uncertain about the result.
oserr_t
VirtioNetStop(
    _InOut_ VirtioNetDevice_t* device,
    _In_    uint64_t           run,
    _In_    uint64_t           fence)
{
    VirtioNetSession_t* session = &device->Session;
    uint64_t before;
    oserr_t  status;
    
    if (!run || run != session->Run) {
        return OS_ENOENT;
    }
    
    if (session->State == VIRTIO_NET_STOPPED) {
        return fence == session->StopFence ? OS_EOK : OS_EINVALPARAMS;
    }
    
    if (session->State == VIRTIO_NET_STOPPING) {
        if (fence != session->StopFence) {
            return OS_EINVALPARAMS;
        }
    } else {
        if ((session->State != VIRTIO_NET_RUNNING && session->State != VIRTIO_NET_PREPARED) ||
            fence != session->Progress.consumed_batch_id) {
            return OS_EBUSY;
        }
        session->StopFence = fence;
        session->State = VIRTIO_NET_STOPPING;
    }

    // Queue reset may finish packets that were still in progress. Remember the
    // earlier position so those newly produced results are sent to the client.
    before = session->Progress.highest_completion_sequence;
    status = VirtioNetQueuesReset(device);
    if (status != OS_EOK) {
        return status;
    }

    session->Counters.resets++;
    session->StopBarrier = session->Progress.highest_completion_sequence;
    session->State = VIRTIO_NET_STOPPED;
    session->Faulted = false;

    VirtioNetPushCompletions(device, before);
    return OS_EOK;
}

// Close is the final boundary for a session: stop device access, release its
// mapped pools, and retain enough identity to reject stale requests. The close
// response itself confirms this boundary, so later completion acknowledgements
// are not needed.
oserr_t
VirtioNetClose(
    _InOut_ VirtioNetDevice_t* device)
{
    VirtioNetSession_t* session = &device->Session;
    oserr_t             status;

    session->State = VIRTIO_NET_STOPPING;
    status = VirtioNetQueuesReset(device);
    if (status != OS_EOK) {
        return status;
    }
    
    VirtioNetPoolsDetach(device);
    device->Closed[device->ClosedCount++] =
        (VirtioNetClosedSession_t) {
            session->Identity,
            session->Owner
        };
    
    memset(session, 0, sizeof(VirtioNetSession_t));
    return OS_EOK;
}
