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
 * Packet queue setup, packet handoff, and completion handling.
 * 
 */

#include "virtio-net.h"
#include <ddk/utils.h>
#include <io.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief Count the most descriptors one packet in a pool can need.
 *
 * Clients choose where a packet starts inside its slot, so every memory
 * segment touched by a slot is counted. The private header always takes one
 * descriptor, and transmit packets may also need one for short-frame padding.
 *
 * @param device Device whose session owns the pool.
 * @param direction Direction of the pool to inspect.
 * @return Descriptor bound for one packet, or 0 if no such pool is mapped.
 */
static size_t
__PacketDescriptorBound(
    _In_ VirtioNetDevice_t*            device,
    _In_ enum ctt_netadapter_direction direction)
{
    for (uint32_t p = 0; p < device->Session.PoolCount; ++p) {
        VirtioNetPool_t* pool = &device->Session.Pools[p];
        size_t           worst = 0;

        if (!pool->Mapped || pool->Description.direction != direction) {
            continue;
        }

        for (uint32_t s = 0; s < pool->Description.slot_count; ++s) {
            size_t start = pool->Description.region_offset +
                           (size_t)s * pool->Description.slot_size;
            size_t end = start + pool->Description.slot_size;
            size_t base = 0;
            size_t segments = 0;

            for (int i = 0; i < pool->ScatterGather.Count; ++i) {
                size_t length = pool->ScatterGather.Entries[i].Length;
                if (base < end && base + length > start) {
                    segments++;
                }
                base += length;
            }
            if (segments > worst) {
                worst = segments;
            }
        }

        // Packets needing more data descriptors than this are refused at submit.
        if (worst > 34) {
            worst = 34;
        }
        worst++;
        if (direction == CTT_NETADAPTER_DIRECTION_TX) {
            worst++;
        }
        return worst;
    }
    return 0;
}

/**
 * @brief Make sure a created queue can hold the packets a run depends on.
 *
 * Queue creation may shrink the requested size to what the device supports.
 * A queue that cannot hold the required packets would let preparation succeed
 * while start could never be satisfied, so it is rejected instead.
 *
 * @param queue Queue that was just created.
 * @param required Number of descriptors the run needs at once.
 * @return OS_EOK when the queue is large enough, otherwise an error.
 */
static oserr_t
__CheckQueueCapacity(
    _In_ VirtioSplitQueue_t* queue,
    _In_ size_t              required)
{
    VirtioQueueStats_t stats;
    oserr_t            status;

    status = VirtioSplitQueueGetStats(queue, &stats);
    if (status != OS_EOK) {
        return status;
    }
    if (stats.QueueSize < required) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Prepare the packet queues for a new device run.
 *
 * The device needs its supported features and current configuration before
 * packets can be exchanged. Queue preparation does not make the device live;
 * startup does that only after the rest of the run is ready.
 *
 * @param device Device whose packet queues are being prepared.
 * @return OS_EOK on success, or the first error that prevents preparation.
 */
oserr_t
VirtioNetQueuesPrepare(
    _InOut_ VirtioNetDevice_t* device)
{
    oserr_t status;

    status = VirtioPciNegotiateFeatures(
        &device->Transport,
        VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS,
        VIRTIO_NET_F_MAC,
        &device->Features
    );
    if (status != OS_EOK) {
        return status;
    }

    status = VirtioNetReadConfiguration(device);
    if (status != OS_EOK) {
        return status;
    }

    status = VirtioSplitQueueCreate(
        &device->Transport,
        0,
        VIRTIO_NET_QUEUE_SIZE,
        &device->ReceiveQueue
    );
    if (status != OS_EOK) {
        return status;
    }

    // Start waits for min_rx_slots receive packets to be queued at once.
    status = __CheckQueueCapacity(
        device->ReceiveQueue,
        __PacketDescriptorBound(
            device,
            CTT_NETADAPTER_DIRECTION_RX
        ) * device->Info.min_rx_slots
    );
    if (status != OS_EOK) {
        return status;
    }

    status = VirtioSplitQueueCreate(
        &device->Transport,
        1,
        VIRTIO_NET_QUEUE_SIZE,
        &device->TransmitQueue
    );
    if (status != OS_EOK) {
        return status;
    }

    // Keep the device from using receive buffers until startup has finished
    // setting up everything this run depends on.
    return __CheckQueueCapacity(
        device->TransmitQueue,
        __PacketDescriptorBound(device, CTT_NETADAPTER_DIRECTION_TX)
    );
}

/**
 * @brief Stop the device and release the queues from its current run.
 *
 * Reset must happen before queue memory is released, so the device cannot
 * continue using it. Packets still waiting in the queues are reported as
 * cancelled, allowing their owners to reclaim them safely.
 *
 * @param device Device whose queues are being reset.
 * @return OS_EOK on success, or an error if reset or queue cleanup fails.
 */
oserr_t
VirtioNetQueuesReset(
    _InOut_ VirtioNetDevice_t* device)
{
    VirtioQueueCompletion_t completion;
    oserr_t                 status;
    VirtioSplitQueue_t**    queues[] = {
        &device->ReceiveQueue,
        &device->TransmitQueue
    };

    status = VirtioPciReset(&device->Transport);
    if (status != OS_EOK) {
        return status;
    }

    for (unsigned int i = 0; i < 2; ++i) {
        if (*queues[i] == NULL) {
            continue;
        }

        while ((status = VirtioSplitQueueAbort(*queues[i], &completion)) == OS_EOK) {
            VirtioNetComplete(device, completion.Context, OS_ECANCELLED, 0);
        }

        if (status != OS_ENOENT) {
            return status;
        }

        status = VirtioSplitQueueDestroy(*queues[i]);
        if (status != OS_EOK) {
            return status;
        }
        *queues[i] = NULL;
    }
    return OS_EOK;
}

/**
 * @brief Give one packet slot to the device for receiving or sending.
 *
 * Receive slots must be writable by the device, while transmit slots must
 * remain readable by it. Short Ethernet transmissions are padded with fresh
 * data so bytes left in old shared memory are never exposed; the packet
 * completion still describes the caller's original frame.
 *
 * @param device Device that will process the packet.
 * @param slot Packet slot to give to the device.
 * @return The queue submission result.
 */
oserr_t
VirtioNetQueuePacket(
    _In_ VirtioNetDevice_t* device,
    _In_ VirtioNetSlot_t*   slot)
{
    VirtioNetPool_t*    pool = slot->Pool;
    bool                receive = pool->Description.direction == CTT_NETADAPTER_DIRECTION_RX;
    VirtioQueueBuffer_t buffers[36];
    uint16_t            count = 1;
    uint16_t            flags = receive ? VIRTIO_SPLIT_DESC_F_WRITE : 0;
    size_t              remaining = receive ? VIRTIO_NET_FRAME_SIZE : slot->Packet.length;
    size_t              offset = pool->Description.region_offset +
                                 (size_t)slot->Packet.id.slot_id * pool->Description.slot_size +
                                 slot->Packet.data_offset;
    int                 index;
    size_t              segmentOffset;
    uintptr_t           metadata;
    oserr_t             status;
    
    status = SHMSGTableOffset(&pool->ScatterGather, offset, &index, &segmentOffset);
    if (status != OS_EOK) {
        return status;
    }

    metadata = device->MetadataSg.Entries[0].Address + slot->MetadataOffset;
    memset(
        (char*)SHMBuffer(&device->Metadata) + slot->MetadataOffset,
        0,
        VIRTIO_NET_METADATA_STRIDE
    );
    buffers[0] = (VirtioQueueBuffer_t) {
        metadata,
        sizeof(VirtioNetHeader_t),
        flags
    };
    
    while (remaining && index < pool->ScatterGather.Count && count < 35) {
        SHMSG_t* segment = &pool->ScatterGather.Entries[index++];
        size_t   length = segment->Length - segmentOffset;
        
        if (length > remaining) {
            length = remaining;
        }

        buffers[count++] = (VirtioQueueBuffer_t) {
            segment->Address + segmentOffset,
            (uint32_t)length,
            flags
        };
        remaining -= length;
        segmentOffset = 0;
    }
    if (remaining) {
        return OS_ENOTSUPPORTED;
    }

    // Ethernet short-frame padding must not expose old pool contents. The used
    // TX completion still reports the caller's original (unpadded) frame length.
    if (!receive && slot->Packet.length < 60) {
        buffers[count++] = (VirtioQueueBuffer_t) {
            metadata + sizeof(VirtioNetHeader_t),
            60 - slot->Packet.length,
            0
        };
    }

    return VirtioSplitQueueSubmit(
        receive ? device->ReceiveQueue : device->TransmitQueue,
        buffers,
        count,
        slot,
        NULL
    );
}

/**
 * @brief Decide whether an incoming frame is meant for this adapter.
 *
 * Passing only this adapter's address and the shared broadcast address keeps
 * unrelated traffic from being delivered to a session that did not request it.
 *
 * @param device Adapter whose address is accepted.
 * @param frame Beginning of the incoming Ethernet frame.
 * @return True when the frame should be accepted.
 */
static bool
__AcceptDestination(
    _In_ VirtioNetDevice_t* device,
    _In_ const uint8_t*     frame)
{
    // Without a broader receive mode, only traffic for this adapter or every
    // adapter should be passed to the session.
    struct ctt_netadapter_mac* mac = &device->Info.current_mac;
    bool                       broadcast = true;
    const uint8_t              address[] = {
        mac->octet0, mac->octet1, mac->octet2,
        mac->octet3, mac->octet4, mac->octet5
    };
    
    for (int i = 0; i < 6; ++i) {
        broadcast &= frame[i] == 0xff;
    }
    return broadcast || !memcmp(frame, address, sizeof(address));
}

/**
 * @brief Tell the active session that packet processing has failed.
 *
 * The notification lets the client decide how to respond, while leaving
 * normal stop or close handling responsible for returning resources.
 *
 * @param device Device with the affected session.
 * @param status Error that caused the fault.
 */
void
VirtioNetFault(
    _InOut_ VirtioNetDevice_t* device,
    _In_ oserr_t               status)
{
    struct ctt_netadapter_fault fault = { .status = status };

    // Report a fault once per active session; repeated queue errors add no
    // useful information to the client.
    if (!device->Session.Active || device->Session.Faulted) {
        return;
    }
    device->Session.Faulted = true;

    // A fault tells the client what went wrong, but does not end the session.
    // Stop or close remains responsible for returning packet and queue memory.
    ctt_netadapter_event_fault_single(
        VirtioNetServer(),
        device->Session.Owner,
        &device->Session.Identity,
        &fault
    );
}

/**
 * @brief Check whether a received frame uses only supported header settings.
 *
 * Frames must contain a complete Ethernet header and fit the receive buffer.
 * This driver accepts plain frames only; it does not support extra processing
 * requests or frames split across multiple receive buffers. A reported buffer
 * count of zero is accepted because some devices use it for a single buffer.
 *
 * @param header Header reported with the received frame.
 * @param length Total received length, including the VirtIO header.
 * @return True when the header and frame length are safe to deliver.
 */
static bool
__IsValidReceiveCompletion(
    _In_ const VirtioNetHeader_t* header,
    _In_ uint32_t                 length)
{
    // Reject truncated Ethernet frames and lengths beyond the receive buffer.
    if (length < sizeof(*header) + 14 ||
        length > sizeof(*header) + VIRTIO_NET_FRAME_SIZE) {
        return false;
    }

    // The session accepts plain frames, so device-side processing must be off.
    if (header->Flags || header->GsoType) {
        return false;
    }

    // More than one buffer would describe data this queue did not provide.
    if (header->NumBuffers > 1) {
        return false;
    }
    return true;
}

/**
 * @brief Check and finish one received packet.
 *
 * Frames that are malformed or not meant for this adapter are not delivered.
 * Unwanted frames reuse their slot, while failures that leave its ownership
 * uncertain stop polling so normal fault handling can take over.
 *
 * @param device Device that owns the receive queue and session.
 * @param slot Slot associated with the completed frame.
 * @param completion Result reported for this frame.
 * @return True to continue polling; false when polling must stop.
 */
static bool
__HandleReceiveCompletion(
    _InOut_ VirtioNetDevice_t*             device,
    _InOut_ VirtioNetSlot_t*               slot,
    _In_    const VirtioQueueCompletion_t* completion)
{
    VirtioNetHeader_t header;
    oserr_t           status;
    uint32_t          length;

    memcpy(
        &header,
        (char*)SHMBuffer(&device->Metadata) + slot->MetadataOffset,
        sizeof(header)
    );
    if (!__IsValidReceiveCompletion(&header, completion->Length)) {
        VirtioNetComplete(device, slot, OS_EPROTOCOL, 0);
        return true;
    }

    length = completion->Length - sizeof(header);
    if (__AcceptDestination(device, VirtioNetPacketBytes(slot))) {
        VirtioNetComplete(device, slot, OS_EOK, length);
        return true;
    }

    device->Session.Counters.rx_dropped++;
    
    // Keep unwanted traffic from reaching the client, while making the slot
    // available for another frame.
    status = VirtioNetQueuePacket(device, slot);
    if (status == OS_EOK) {
        return true;
    }
    if (status == OS_EINPROGRESS) {
        VirtioNetFault(device, status);
        return false;
    }

    VirtioNetComplete(device, slot, status, 0);
    return true;
}

/**
 * @brief Handle completed packets from one device queue.
 *
 * Incoming packets are checked before being delivered, and packets outside
 * the session's destination filter are returned for reuse. A fixed amount of
 * work per call keeps a busy queue from delaying other device activity.
 *
 * @param device Device that owns the queue and packet slots.
 * @param queue Queue whose completed packets should be handled.
 * @param receive True for received packets, false for transmitted packets.
 */
static void
__PollQueue(
    _InOut_ VirtioNetDevice_t* device,
    _In_ VirtioSplitQueue_t*   queue,
    _In_ bool                  receive)
{
    unsigned int signal = 1;

    if (queue == NULL) {
        return;
    }

    for (unsigned budget = 0; budget < 64; ++budget) {
        VirtioQueueCompletion_t completion;
        VirtioNetSlot_t*        slot;
        oserr_t                 status;

        status = VirtioSplitQueuePoll(queue, &completion);
        if (status == OS_ENOENT) {
            return;
        }
        if (status != OS_EOK) {
            VirtioNetFault(device, status);
            return;
        }

        slot = completion.Context;
        if (receive) {
            if (!__HandleReceiveCompletion(device, slot, &completion)) {
                return;
            }
            continue;
        }

        if (completion.Length != 0) {
            VirtioNetComplete(device, slot, OS_EPROTOCOL, 0);
            continue;
        }
        VirtioNetComplete(device, slot, OS_EOK, slot->Packet.length);
    }

    // More packets may be ready. Ask for another turn so other device work can
    // run between batches, even when incoming traffic is being filtered out.
    atomic_fetch_or(&device->InterruptResource.PendingStatus, VIRTIO_ISR_QUEUE_INTERRUPT);
    (void)write(device->EventDescriptor, &signal, sizeof(signal));
}

/**
 * @brief Process ready packet work for a running session.
 *
 * Both receive and transmit queues are checked so packet ownership can be
 * returned promptly. Newly completed packets are then made available to the
 * client as one progress update.
 *
 * @param device Device whose running session is being serviced.
 */
void
VirtioNetPoll(
    _InOut_ VirtioNetDevice_t* device)
{
    uint64_t before;

    // Queue activity belongs to a running session; other states must not
    // publish packet completions to the client.
    if (!device->Session.Active || device->Session.State != VIRTIO_NET_RUNNING) {
        return;
    }

    before = device->Session.Progress.highest_completion_sequence;
    __PollQueue(device, device->ReceiveQueue, true);
    __PollQueue(device, device->TransmitQueue, false);
    VirtioNetPushCompletions(device, before);
}
