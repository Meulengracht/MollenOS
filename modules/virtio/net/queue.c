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
 * Packet queue setup, submission, and completion.
 * 
 */

#include "virtio-net.h"
#include <ddk/utils.h>
#include <io.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief Find the maximum descriptor count for one packet in a pool.
 *
 * A packet can start anywhere in its slot, so count all mapped segments in
 * each slot. Allow one descriptor for the private header and, for transmit
 * packets, one for short-frame padding.
 *
 * @param device Device with the registered pool.
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
            uint32_t segments;

            (void)DMAMappingSegments(pool->Slots[s].Mapping, &segments);
            if (segments > worst) {
                worst = segments;
            }
        }

        // Submission rejects packets that need more than 34 data descriptors.
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
 * @brief Check that the created queue has enough descriptors for this run.
 *
 * Queue creation can reduce the requested size to fit the device's limit.
 * Reject smaller queues here, since startup requires enough space for the
 * initial packets.
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
 * Negotiate features and read the device configuration before creating the
 * queues. Startup sets DRIVER_OK once the rest of the run is ready.
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

    // Leave DRIVER_OK clear until startup has finished preparing this run.
    return __CheckQueueCapacity(
        device->TransmitQueue,
        __PacketDescriptorBound(device, CTT_NETADAPTER_DIRECTION_TX)
    );
}

static oserr_t
__EndPacket(
    _InOut_ VirtioNetSlot_t* slot,
    _In_    size_t           completed)
{
    oserr_t status;

    if (!slot->DmaActive) {
        return OS_EOK;
    }

    // The used entry or a successful reset must confirm that the device has
    // stopped using this slot. If cache updates or copying fail, leave DmaActive
    // set so close can retry cleanup.
    status = DMAEndTransfer(slot->Mapping, completed);
    if (status == OS_EOK) {
        slot->DmaActive = false;
    }
    return status;
}

/**
 * @brief Stop the device and release the queues from its current run.
 *
 * Reset must finish before we free queue memory. Pending packets are completed
 * with OS_ECANCELLED after DMA cleanup, so the client can use their slots again.
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

    // DMA cleanup may have failed after completion or before submission.
    // Check every slot, including those with no remaining queue entry.
    for (uint32_t p = 0; p < device->Session.PoolCount; p++) {
        for (uint32_t n = 0; n < device->Session.Pools[p].Description.slot_count; n++) {
            VirtioNetSlot_t* slot = &device->Session.Pools[p].Slots[n];
            if (!slot->Owned) {
                continue;
            }

            status = __EndPacket(slot, 0);
            if (status != OS_EOK) {
                return status;
            }
            VirtioNetComplete(device, slot, OS_ECANCELLED, 0);
        }
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
 * @brief Submit one packet slot for receiving or sending.
 *
 * The device must be able to write receive slots and read transmit slots.
 * Short Ethernet frames are padded with zeroes to avoid sending old shared
 * memory contents. Completion reports the original frame length.
 *
 * @param device Device that will process the packet.
 * @param slot Packet slot to submit.
 * @return The queue submission result.
 */
oserr_t
VirtioNetQueuePacket(
    _In_ VirtioNetDevice_t* device,
    _In_ VirtioNetSlot_t*   slot)
{
    VirtioNetPool_t*         pool = slot->Pool;
    bool                     receive = pool->Description.direction == CTT_NETADAPTER_DIRECTION_RX;
    VirtioQueueBuffer_t      buffers[36];
    uint16_t                 count = 1;
    uint16_t                 flags = receive ? VIRTIO_SPLIT_DESC_F_WRITE : 0;
    size_t                   remaining = receive ? VIRTIO_NET_FRAME_SIZE : slot->Packet.length;
    size_t                   segmentOffset = slot->Packet.data_offset;
    uint32_t                 index = 0;
    uint32_t                 segmentCount;
    const struct DMASegment* segments;
    uint64_t                 metadata;
    oserr_t                  status;
    
    status = VirtioNetMetadataAddress(device, slot->MetadataOffset, &metadata);
    if (status != OS_EOK) {
        return status;
    }
    
    segments = DMAMappingSegments(slot->Mapping, &segmentCount);
    while (index < segmentCount && segmentOffset >= segments[index].Length) {
        segmentOffset -= segments[index++].Length;
    }

    memset(
        (char*)DMABufferData(device->Metadata) + slot->MetadataOffset,
        0,
        VIRTIO_NET_METADATA_STRIDE
    );
    buffers[0] = (VirtioQueueBuffer_t) {
        metadata,
        sizeof(VirtioNetHeader_t),
        flags
    };
    
    while (remaining && index < segmentCount && count < 35) {
        const struct DMASegment* segment = &segments[index++];
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

    // Pad short frames from the cleared metadata buffer. Completion still
    // reports the caller's original frame length.
    if (!receive && slot->Packet.length < 60) {
        buffers[count++] = (VirtioQueueBuffer_t) {
            metadata + sizeof(VirtioNetHeader_t),
            60 - slot->Packet.length,
            0
        };
    }

    status = DMABeginTransfer(
        slot->Mapping,
        slot->Packet.data_offset,
        receive ? VIRTIO_NET_FRAME_SIZE : slot->Packet.length
    );
    if (status != OS_EOK) {
        return status;
    }

    slot->DmaActive = true;
    status = VirtioSplitQueueSubmit(
        receive ? device->ReceiveQueue : device->TransmitQueue,
        buffers,
        count,
        slot,
        NULL
    );
    if (status != OS_EOK && status != OS_EINPROGRESS) {
        // Submission failed before any descriptor reached the device. End DMA
        // before rejecting the packet. If that fails, keep the slot accepted
        // so reset can finish cleanup.
        if (__EndPacket(slot, 0) != OS_EOK) {
            return OS_EINPROGRESS;
        }
    }
    return status;
}

/**
 * @brief Check the destination address of an incoming frame.
 *
 * Accept this adapter's MAC address and broadcasts. Other destinations are
 * outside the session's receive filter.
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
    // The session accepts this adapter's MAC address and broadcasts only.
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
 * Notify the client once. Stop or close must still release the session's
 * packet and queue resources.
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

    // Send only the first fault for an active session.
    if (!device->Session.Active || device->Session.Faulted) {
        return;
    }
    device->Session.Faulted = true;

    // Keep the session open. Stop or close handles packet and queue cleanup.
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
 * Only plain frames in one receive buffer are supported. Extra processing
 * requests are rejected. A buffer count of zero is accepted because some
 * devices use it for a single buffer.
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
 * Reject malformed frames and requeue slots for unwanted destinations.
 * If requeueing returns OS_EINPROGRESS, report a fault and stop this polling
 * pass. Stop or close handles cleanup.
 *
 * @param device Device with the receive queue and session.
 * @param slot Slot associated with the completed frame.
 * @param completion Result reported for this frame.
 * @return True to continue polling, false when polling must stop.
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
        (char*)DMABufferData(device->Metadata) + slot->MetadataOffset,
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
    
    // Requeue the slot for another frame without reporting this one to the client.
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
 * Check received packets before delivery and requeue slots for unwanted
 * destinations. Process at most 64 completions per call so other device work
 * can run between batches.
 *
 * @param device Device with the queue and packet slots.
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
        
        // Received bytes may need copying from a staging buffer or cache updates.
        // Finish DMA for the checked length before reading the frame or reporting
        // completion to the client.
        status = __EndPacket(
            slot,
            receive && completion.Length >= sizeof(VirtioNetHeader_t) &&
                completion.Length <= sizeof(VirtioNetHeader_t) + VIRTIO_NET_FRAME_SIZE
                ? completion.Length - sizeof(VirtioNetHeader_t) : 0
        );
        if (status != OS_EOK) {
            VirtioNetFault(device, status);
            return;
        }
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

    // Schedule another pass in case more packets are ready. Other device work
    // can run between batches, including when every received frame is dropped.
    atomic_fetch_or(&device->InterruptResource.PendingStatus, VIRTIO_ISR_QUEUE_INTERRUPT);
    (void)write(device->EventDescriptor, &signal, sizeof(signal));
}

/**
 * @brief Process ready packet work for a running session.
 *
 * Requires an active, running session. Poll both queues and send one progress
 * update for the newly completed packets.
 *
 * @param device Device whose running session is being serviced.
 */
void
VirtioNetPoll(
    _InOut_ VirtioNetDevice_t* device)
{
    uint64_t before;

    // Only an active, running session may report packet completions.
    if (!device->Session.Active || device->Session.State != VIRTIO_NET_RUNNING) {
        return;
    }

    before = device->Session.Progress.highest_completion_sequence;
    __PollQueue(device, device->ReceiveQueue, true);
    __PollQueue(device, device->TransmitQueue, false);
    VirtioNetPushCompletions(device, before);
}
