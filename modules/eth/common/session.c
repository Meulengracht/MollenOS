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

#include "netadapter-private.h"
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

// Zero is an invalid session ID. IDs are never reused during this server lifetime.
static uint64_t g_nextSession = 1;

static oserr_t
__ReserveCloseRecord(
    _InOut_ EthernetDevice_t* device)
{
    EthernetClosedSession_t* records;
    size_t                   capacity;
    size_t                   maximum = SIZE_MAX / sizeof(*records);

    if (device->Common->ClosedCount < device->Common->ClosedCapacity) {
        return OS_EOK;
    }

    if (device->Common->ClosedCount == maximum) {
        return OS_EOVERFLOW;
    }

    // Reserve before opening so close cannot fail for lack of bookkeeping
    // memory. Double the allocation, capped before the byte-size multiplication.
    capacity = device->Common->ClosedCapacity;
    if (!capacity) {
        capacity = ETHERNET_INITIAL_CLOSED_SESSIONS;
    } else {
        capacity = capacity > maximum / 2 ? maximum : capacity * 2;
    }

    records = realloc(device->Common->Closed, capacity * sizeof(EthernetClosedSession_t));
    if (records == NULL) {
        ERROR("__ReserveCloseRecord: device %u allocation failed", device->Common->DeviceId);
        return OS_EOOM;
    }

    device->Common->Closed = records;
    device->Common->ClosedCapacity = capacity;
    return OS_EOK;
}

oserr_t
EthernetOpen(
    _InOut_ EthernetDevice_t*            device,
    _In_    const struct gracht_message* message,
    _In_    uint64_t                     features)
{
    EthernetSession_t* session = &device->Common->Session;
    oserr_t            status;
    size_t             i;

    // No optional contract features are implemented. Do not accept and
    // silently ignore features that change the client's expected behavior.
    if (features) {
        return OS_ENOTSUPPORTED;
    }

    if (session->Active && (session->Owner != message->client ||
        session->Principal != message->security.principal)) {
        return OS_EBUSY;
    }

    if (!session->Active && g_nextSession == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    if (!session->Active) {
        // OPEN has no operation key. Reusing an already closed endpoint would
        // make a delayed old OPEN indistinguishable from a new one. Require a
        // fresh client endpoint and keep closed-session records for retries.
        for (i = 0; i < device->Common->ClosedCount; ++i) {
            if (device->Common->Closed[i].Owner == message->client) {
                return OS_ENOENT;
            }
        }

        status = __ReserveCloseRecord(device);
        if (status != OS_EOK) {
            return status;
        }
    }

    // Register the reply endpoint before exposing a new session. A failed
    // registration must not consume a session ID or change the link snapshot.
    if (gracht_server_register_client(message)) {
        ERROR("EthernetOpen: device %u client registration failed", device->Common->DeviceId);
        return OS_EOOM;
    }

    if (!session->Active) {
        // Link sequence one is the initial snapshot for this session.
        // The server handle distinguishes IDs from a previous module run.
        device->Common->Link.sequence = 1;
        session->Active = true;
        session->Owner = message->client;
        session->Principal = message->security.principal;
        session->Identity = (struct ctt_netadapter_session) {
            g_nextSession++,
            EthernetGeneration()
        };
        session->State = ETHERNET_OPENED;
    }
    return OS_EOK;
}

bool
EthernetIsIdle(
    _In_ const EthernetDevice_t* device)
{
    const EthernetSession_t* session = &device->Common->Session;

    // Pool changes require no outstanding packets and all completions acknowledged.
    return (session->State == ETHERNET_OPENED || session->State == ETHERNET_STOPPED) &&
           !session->Outstanding[0] && !session->Outstanding[1] &&
           session->Progress.highest_completion_sequence ==
                   session->Progress.retired_completion_sequence;
}

oserr_t
EthernetConfigure(
    _InOut_ EthernetDevice_t* device,
    _In_    uint32_t          mtu,
    _In_    uint32_t          filter)
{
    if (!EthernetIsIdle(device)) {
        return OS_EBUSY;
    }

    // Pool layouts and queued frames use a fixed 1500-byte MTU. Only directed
    // unicast and broadcast reception are implemented by the MAC filter.
    if (mtu != ETHERNET_MTU ||
        filter != (CTT_NETADAPTER_RX_FILTER_UNICAST | CTT_NETADAPTER_RX_FILTER_BROADCAST)) {
        return OS_ENOTSUPPORTED;
    }

    device->Common->Session.Configured = true;
    return OS_EOK;
}

static bool
__CanPrepare(
    _In_ const EthernetDevice_t* device)
{
    const EthernetSession_t* session = &device->Common->Session;

    if (!EthernetIsIdle(device) || !session->Configured || session->Faulted) {
        return false;
    }
    return session->Progress.retired_batch_id == session->Progress.consumed_batch_id;
}

oserr_t
EthernetPrepare(
    _InOut_ EthernetDevice_t* device,
    _In_    uint64_t          run)
{
    EthernetSession_t* session = &device->Common->Session;
    unsigned           directions = 0;
    oserr_t            status;

    // Repeated prepare for the current run returns the existing result.
    // A new run requires all earlier batches and completions acknowledged.
    if (run && run == session->Run &&
        (session->State == ETHERNET_PREPARED || session->State == ETHERNET_RUNNING)) {
        return OS_EOK;
    }

    if (!run || session->Run == UINT64_MAX || run != session->Run + 1) {
        return OS_ENOENT;
    }

    if (!__CanPrepare(device)) {
        return OS_EBUSY;
    }

    for (uint32_t i = 0; i < session->PoolCount; ++i) {
        if (session->Pools[i].Mapped) {
            directions |= session->Pools[i].Description.direction;
        }
    }
    
    // Require one mapped pool in each direction before creating queues.
    status = directions == (CTT_NETADAPTER_DIRECTION_RX | CTT_NETADAPTER_DIRECTION_TX)
                 ? EthernetQueuesPrepare(device)
                 : OS_EINVALPARAMS;
    if (status == OS_EOK) {
        session->Run = run;
        session->State = ETHERNET_PREPARED;
    } else if (device->Common->QueuesPrepared) {
        EthernetFault(device, status);
    }
    return status;
}

static bool
__CanStart(
    _In_ const EthernetDevice_t* device,
    _In_ uint64_t                fence)
{
    const EthernetSession_t* session = &device->Common->Session;

    if (session->State != ETHERNET_PREPARED || session->Faulted) {
        return false;
    }

    // The start fence must include every posted RX batch. Outstanding[1]
    // counts RX slots against the minimum supplied by the MAC at registration.
    return fence == session->Progress.consumed_batch_id &&
           session->Outstanding[1] >= device->Common->Info.min_rx_slots;
}

oserr_t
EthernetStart(
    _InOut_ EthernetDevice_t* device,
    _In_    uint64_t          run,
    _In_    uint64_t          fence)
{
    EthernetSession_t* session = &device->Common->Session;
    oserr_t            status;

    if (!run || run != session->Run) {
        return OS_ENOENT;
    }

    if (session->State == ETHERNET_RUNNING) {
        return fence == session->StartFence ? OS_EOK : OS_EINVALPARAMS;
    }

    if (!__CanStart(device, fence)) {
        return OS_EBUSY;
    }

    status = device->Operations->Start(device);
    if (status == OS_EOK) {
        session->StartFence = fence;
        session->State = ETHERNET_RUNNING;
    } else {
        EthernetFault(device, status);
    }
    return status;
}

// The stop fence must match the last consumed batch. Stop accepting packets,
// reset the queues and save the last completion sequence. The caller can retry
// with the same fence if the response is lost.
oserr_t
EthernetStop(
    _InOut_ EthernetDevice_t* device,
    _In_    uint64_t          run,
    _In_    uint64_t          fence)
{
    EthernetSession_t* session = &device->Common->Session;
    uint64_t           before;
    oserr_t            status;

    if (!run || run != session->Run) {
        return OS_ENOENT;
    }

    if (session->State == ETHERNET_STOPPED) {
        return fence == session->StopFence ? OS_EOK : OS_EINVALPARAMS;
    }

    if (session->State == ETHERNET_STOPPING) {
        if (fence != session->StopFence) {
            return OS_EINVALPARAMS;
        }
    } else {
        if ((session->State != ETHERNET_RUNNING && session->State != ETHERNET_PREPARED) ||
            fence != session->Progress.consumed_batch_id) {
            return OS_EBUSY;
        }
        session->StopFence = fence;
        session->State = ETHERNET_STOPPING;
    }

    // Reset may complete pending packets. Save the current sequence so we can
    // send those new completions to the client afterwards.
    before = session->Progress.highest_completion_sequence;
    status = EthernetQueuesReset(device);
    if (status != OS_EOK) {
        return status;
    }

    session->Counters.resets++;
    session->StopBarrier = session->Progress.highest_completion_sequence;
    session->State = ETHERNET_STOPPED;
    session->Faulted = false;

    EthernetPushCompletions(device, before);
    return OS_EOK;
}

// Stop device access and release the session's mapped pools. Save its identity
// so repeated closes can succeed without changing a newer session. A successful
// close response confirms cleanup is complete, so no acknowledgements are needed.
oserr_t
EthernetClose(
    _InOut_ EthernetDevice_t* device)
{
    EthernetSession_t* session = &device->Common->Session;
    oserr_t            status;

    session->State = ETHERNET_STOPPING;
    status = EthernetQueuesReset(device);
    if (status != OS_EOK) {
        return status;
    }

    status = EthernetPoolsDetach(device);
    if (status != OS_EOK) {
        return status;
    }

    // Open reserved this record before admitting the session. Closing must not
    // allocate after the hardware and pools have already been released.
    device->Common->Closed[device->Common->ClosedCount++] =
        (EthernetClosedSession_t) {
        session->Identity,
        session->Owner, session->Principal
    };

    memset(session, 0, sizeof(EthernetSession_t));
    return OS_EOK;
}

void
EthernetReportReceiveDrop(
    _InOut_ EthernetDevice_t*        device,
    _In_    enum EthernetReceiveDrop reason)
{
    struct ctt_netadapter_counters* counters = &device->Common->Session.Counters;

    switch (reason) {
        case EthernetReceiveFiltered:
            counters->rx_dropped++;
            break;
        case EthernetReceiveNoBuffer:
            counters->rx_no_buffer++;
            break;
        case EthernetReceiveError:
            counters->rx_errors++;
            break;
    }
}

void
EthernetFault(
    _InOut_ EthernetDevice_t* device,
    _In_    oserr_t           status)
{
    if (!device->Common->Session.Active || device->Common->Session.Faulted) {
        return;
    }

    device->Common->Session.Faulted = true;
    WARNING("EthernetFault: device %u failed (%u)", device->Common->DeviceId, status);

    // Block new packets even if the notification cannot reach the client.
    // Stop or close must still confirm that DMA has finished before cleanup.
    EthernetSendFault(device, status);
}

oserr_t
EthernetReadCompletions(
    _In_  const EthernetDevice_t*           device,
    _In_  uint64_t                          after,
    _In_  uint32_t                          limit,
    _Out_ struct ctt_netadapter_completion* records,
    _Out_ uint32_t*                         countOut)
{
    const struct ctt_netadapter_progress* progress = &device->Common->Session.Progress;
    uint32_t                              count;

    // reset count even on errors
    *countOut = 0;

    if (after < progress->retired_completion_sequence) {
        return OS_ENOENT;
    }
    if (after > progress->highest_completion_sequence || !limit || limit > ETHERNET_BATCH) {
        return OS_EINVALPARAMS;
    }

    // 'after' is excluded. Sequence after + 1 is stored at index after
    // because the journal starts at zero and sequences start at one.
    count = progress->highest_completion_sequence - after < limit
                ? (uint32_t)(progress->highest_completion_sequence - after) : limit;
    for (uint32_t i = 0; i < count; ++i) {
        records[i] = device->Common->Session.Journal[(after + i) % ETHERNET_JOURNAL];
    }
    *countOut = count;
    return OS_EOK;
}

bool
EthernetDrain(
    _InOut_ EthernetDevice_t*                device,
    _In_    uint64_t                         request,
    _In_    uint64_t                         after,
    _In_    uint32_t                         limit,
    _In_    const struct ctt_netadapter_ack* ack,
    _Out_   EthernetDrain_t*                 result)
{
    EthernetSession_t* session = &device->Common->Session;

    if (request <= session->DrainId) {
        return false;
    }

    // Drain requests use increasing IDs. Recovery after a lost response uses a
    // new ID and the last completion sequence the client actually received.
    session->DrainId = request;

    // Poll completed descriptors during recovery too, so a lost hardware IRQ
    // does not leave slots pending forever. Poll may send independent pushes.
    EthernetPoll(device);

    result->Status = EthernetAcknowledge(device, ack);
    result->Highest = session->Progress.highest_completion_sequence;
    result->Count = 0;
    if (result->Status == OS_EOK) {
        result->Status = EthernetReadCompletions(
            device,
            after,
            limit,
            result->Records,
            &result->Count
        );
    }
    return true;
}

// The client can acknowledge only batches we have accepted and packets we have
// completed. Keep newer records available until the client acknowledges them.
oserr_t
EthernetAcknowledge(
    _InOut_ EthernetDevice_t*                device,
    _In_    const struct ctt_netadapter_ack* ack)
{
    struct ctt_netadapter_progress* progress = &device->Common->Session.Progress;

    // Check both markers before updating either one. A rejected acknowledgement
    // must leave the saved progress unchanged.
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

// Save one completion in the journal and make the packet slot available again.
// The client can read the saved result if its notification is lost.
void
EthernetComplete(
    _InOut_ EthernetDevice_t* device,
    _InOut_ EthernetSlot_t*   slot,
    _In_    oserr_t           status,
    _In_    uint32_t          length)
{
    EthernetSession_t* session = &device->Common->Session;
    enum ctt_netadapter_direction direction;
    uint64_t sequence;

    if (!slot->Owned) {
        // No completion is pending for this slot. Leave its counters unchanged.
        return;
    }

    direction = slot->Pool->Description.direction;
    // Admission reserves both journal capacity and sequence space. Completion
    // can therefore run during reset without allocation or another limit check.
    sequence = ++session->Progress.highest_completion_sequence;

    // Protocol sequences start at one, while journal indices start at zero.
    session->Journal[(sequence - 1) % ETHERNET_JOURNAL] =
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

// A retried batch must contain the same packet descriptors as the first request.
// Compare the identifiers, buffer range and flags.
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

// Find the packet slot in a mapped pool with the requested direction.
// Check the pool and slot identifiers before using them as array indices.
static oserr_t
__FindPacketSlot(
    _In_  EthernetSession_t*                  session,
    _In_  enum ctt_netadapter_direction       direction,
    _In_  const struct ctt_netadapter_packet* packet,
    _Out_ EthernetPool_t**                    poolOut,
    _Out_ EthernetSlot_t**                    slotOut)
{
    EthernetPool_t* pool;

    if (!packet->id.pool_id || packet->id.pool_id > session->PoolCount) {
        return OS_EINVALPARAMS;
    }

    pool = &session->Pools[packet->id.pool_id - 1];
    if (!pool->Mapped || pool->Description.direction != direction) {
        return OS_EINVALPARAMS;
    }
    // This adapter exposes queue 0 only. Pool IDs start at one, slot IDs at zero.
    if (packet->id.queue_id || packet->id.slot_id >= pool->Description.slot_count) {
        return OS_EINVALPARAMS;
    }

    *poolOut = pool;
    *slotOut = &pool->Slots[packet->id.slot_id];
    return OS_EOK;
}

// Packet data must fit within the registered slot. Check the frame size for TX
// and require enough space for a full frame on RX.
static bool
__PacketSizeValid(
    _In_ const EthernetPool_t*               pool,
    _In_ enum ctt_netadapter_direction       direction,
    _In_ const struct ctt_netadapter_packet* packet)
{
    // No packet flags or offloads are supported. Subtract the validated offset
    // before comparing length so client-supplied values cannot wrap the end.
    if (packet->flags || packet->data_offset > pool->Description.slot_size) {
        return false;
    }
    
    if (packet->length > pool->Description.slot_size - packet->data_offset) {
        return false;
    }
    
    // TX must contain the 14-byte destination/source/type header. Short frame
    // padding and the FCS are added by the MAC, outside the client's slot.
    if (direction == CTT_NETADAPTER_DIRECTION_TX &&
        (packet->length < 14 || packet->length > ETHERNET_FRAME_SIZE)) {
        return false;
    }
    
    if (direction == CTT_NETADAPTER_DIRECTION_RX && packet->length < ETHERNET_FRAME_SIZE) {
        return false;
    }
    return true;
}

// Check the packet and session state before queueing. Every accepted packet
// needs space in the completion journal before the device can use it.
static oserr_t
__AdmitPacket(
    _InOut_ EthernetDevice_t*                   device,
    _In_    enum ctt_netadapter_direction       direction,
    _In_    const struct ctt_netadapter_packet* packet)
{
    EthernetSession_t* session = &device->Common->Session;
    EthernetPool_t*    pool;
    EthernetSlot_t*    slot;
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

    // Save the sequence even if a later check rejects the packet. The same
    // sequence must not be accepted again as a new request.
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
        device->Common->Link.status != CTT_NETADAPTER_LINK_STATUS_UP) {
        return OS_ENOTCONNECTED;
    }

    index = direction == CTT_NETADAPTER_DIRECTION_TX ? 0 : 1;
    outstanding = session->Outstanding[0] + session->Outstanding[1];
    
    // Pending packets also need journal entries when they finish. Count both
    // directions so a full RX queue cannot consume space promised to TX.
    if (session->Outstanding[index] == ETHERNET_SLOTS ||
        session->Progress.highest_completion_sequence -
                        session->Progress.retired_completion_sequence + outstanding >=
                ETHERNET_JOURNAL) {
        return OS_EBUSY;
    }
    if (session->Progress.highest_completion_sequence >= UINT64_MAX - outstanding) {
        return OS_EOVERFLOW;
    }

    slot->Packet = *packet;

    // Reserve its completion before queueing. Once the device can access the
    // packet, we cannot safely withdraw it.
    slot->Owned = true;
    session->Outstanding[index]++;

    status = EthernetQueuePacket(device, slot);
    if (status == OS_EINPROGRESS) {
        // Hardware may already have seen this packet. Report it as accepted
        // and require stop recovery to produce its final completion safely.
        EthernetFault(device, status);
        return OS_EOK;
    }
    if (status != OS_EOK) {
        // Other submit errors guarantee that the MAC did not queue the packet.
        slot->Owned = false;
        session->Outstanding[index]--;
        if (status == OS_EBUSY && index == 0) {
            session->Counters.tx_queue_full++;
        }
    }
    return status;
}

// A retry must match the saved batch, including every packet descriptor.
// New batches must follow the last consumed batch and fit within the window.
static oserr_t
__ValidateBatch(
    _In_  EthernetSession_t*                  session,
    _In_  EthernetBatch_t*                    batch,
    _In_  uint64_t                            run,
    _In_  uint64_t                            batchId,
    _In_  const struct ctt_netadapter_packet* packets,
    _In_  uint32_t                            count,
    _In_  enum ctt_netadapter_direction       direction,
    _Out_ bool*                               fresh)
{
    // reset fresh every time
    *fresh = false;

    if (!run || run != session->Run || !batchId) {
        return OS_ENOENT;
    }
    if (batchId <= session->Progress.retired_batch_id) {
        return OS_ENOENT;
    }
    if (!count || count > ETHERNET_BATCH) {
        return OS_EINVALPARAMS;
    }

    if (batchId <= session->Progress.consumed_batch_id) {
        // Check the saved payload before current run state. A retry must still
        // succeed after stop and must never queue the same packet again.
        if (batch->Id != batchId || batch->Run != run) {
            return OS_EPROTOCOL;
        }
        if (batch->Direction != direction || batch->Count != count) {
            return OS_EPROTOCOL;
        }

        for (uint32_t i = 0; i < count; ++i) {
            if (!__SamePacket(&batch->Packets[i], &packets[i])) {
                return OS_EPROTOCOL;
            }
        }
        return OS_EOK;
    }

    if (session->State != ETHERNET_PREPARED && session->State != ETHERNET_RUNNING) {
        return OS_EBUSY;
    }

    if (direction == CTT_NETADAPTER_DIRECTION_TX && session->State != ETHERNET_RUNNING) {
        return OS_EBUSY;
    }

    if (batchId != session->Progress.consumed_batch_id + 1) {
        return OS_EBUSY;
    }

    if (batchId - session->Progress.retired_batch_id > ETHERNET_WINDOW) {
        return OS_EBUSY;
    }

    *fresh = true;
    return OS_EOK;
}

// Save the batch and each packet's admission result so we can answer a retry
// after a lost response.
static void
__AdmitBatch(
    _InOut_ EthernetDevice_t*                   device,
    _InOut_ EthernetBatch_t*                    batch,
    _In_    uint64_t                            run,
    _In_    uint64_t                            batchId,
    _In_    const struct ctt_netadapter_packet* packets,
    _In_    uint32_t                            count,
    _In_    enum ctt_netadapter_direction       direction)
{
    EthernetSession_t* session = &device->Common->Session;

    *batch = (EthernetBatch_t) {
        .Id = batchId,
        .Run = run,
        .Direction = direction,
        .Count = count
    };
    memcpy(batch->Packets, packets, count * sizeof(*packets));

    // Consume the batch even if some packets are rejected. A retry must return
    // the same saved results.
    for (uint32_t i = 0; i < count; ++i) {
        batch->Admissions[i].id = batch->Packets[i].id;
        batch->Admissions[i].status = __AdmitPacket(
            device,
            direction,
            &batch->Packets[i]
        );
    }
    session->Progress.consumed_batch_id = batchId;
}

// Process each new batch once. A retry returns the saved admission results
// without queueing the packets again.
oserr_t
EthernetAdmitBatch(
    _InOut_ EthernetDevice_t*                      device,
    _In_    uint64_t                               run,
    _In_    uint64_t                               batchId,
    _In_    const struct ctt_netadapter_ack*        ack,
    _In_    const struct ctt_netadapter_packet*     packets,
    _In_    uint32_t                               count,
    _In_    enum ctt_netadapter_direction          direction,
    _Out_   const struct ctt_netadapter_admission** admissionsOut)
{
    EthernetSession_t* session = &device->Common->Session;
    oserr_t            status;
    EthernetBatch_t*   batch;
    bool               fresh = false;

    *admissionsOut = NULL;

    // Batch IDs start at one. ID zero selects a harmless slot until validation
    // rejects it, without underflowing the index calculation.
    batch = &session->Batches[batchId ? (batchId - 1) % ETHERNET_WINDOW : 0];

    status = EthernetAcknowledge(device, ack);
    if (status != OS_EOK) {
        return status;
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

    if (status == OS_EOK) {
        *admissionsOut = batch->Admissions;
    }
    return status;
}
