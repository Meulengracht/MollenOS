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
 *
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#define __TRACE
#define __need_minmax
#define __need_static_assert
#include <ddk/barrier.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <os/memory.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <virtio/virtio.h>

#include "queue.h"

COMPILE_TIME_ASSERT(sizeof(VirtioSplitDescriptor_t) == 16);
COMPILE_TIME_ASSERT(sizeof(VirtioSplitUsedElement_t) == 8);
COMPILE_TIME_ASSERT(offsetof(VirtioSplitAvailableRing_t, Ring) == 4);
COMPILE_TIME_ASSERT(offsetof(VirtioSplitUsedRing_t, Ring) == 4);

static oserr_t
__CommonRead(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint32_t              offset,
    _In_  size_t                width,
    _Out_ uint64_t*             valueOut)
{
    return VirtioPciRegionRead(
        &transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
        offset,
        width,
        valueOut
    );
}

static oserr_t
__CommonWrite(
    _In_ VirtioPciTransport_t* transport,
    _In_ uint32_t              offset,
    _In_ uint64_t              value,
    _In_ size_t                width)
{
    return VirtioPciRegionWrite(
        &transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
        offset,
        value,
        width
    );
}

static oserr_t
__SelectQueue(
    _In_ VirtioPciTransport_t* transport,
    _In_ uint16_t              queueIndex)
{
    return __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueSelect),
        queueIndex,
        sizeof(uint16_t)
    );
}

static uint16_t
__FloorPowerOfTwo(
    _In_ uint16_t value)
{
    uint16_t result = 1;

    while (result <= (uint16_t)(value >> 1)) {
        result <<= 1;
    }
    return result;
}

static oserr_t
__QueueMemoryDestroy(
    _In_ VirtioSplitQueue_t* queue)
{
    oserr_t status;

    // The hardware must be stopped first. DMAFree clears each pointer on
    // success, so we can retry a failed release without freeing memory twice.
    status = DMAFree(&queue->UsedRegion);
    if (status != OS_EOK) {
        return status;
    }

    status = DMAFree(&queue->AvailableRegion);
    if (status != OS_EOK) {
        return status;
    }

    status = DMAFree(&queue->DescriptorRegion);
    if (status != OS_EOK) {
        return status;
    }

    free(queue->Contexts);
    free(queue->Allocated);
    free(queue->ChainLengths);
    free(queue->FreeList);
    return OS_EOK;
}

static uint64_t
__QueueAddress(
    _In_ const struct DMABuffer* buffer)
{
    uint32_t count;

    // Queue memory must have one segment. Use its device address, keeping all
    // 64 bits even on 32-bit targets.
    return DMABufferSegments(buffer, &count)[0].Address;
}

static oserr_t
__QueueMemoryCreate(
    _InOut_ VirtioSplitQueue_t* queue)
{
    size_t descriptorBytes = sizeof(VirtioSplitDescriptor_t) * queue->QueueSize;
    size_t availableBytes  = offsetof(VirtioSplitAvailableRing_t, Ring) +
            (sizeof(uint16_t) * queue->QueueSize) + sizeof(uint16_t);
    size_t usedBytes       = offsetof(VirtioSplitUsedRing_t, Ring) +
            (sizeof(VirtioSplitUsedElement_t) * queue->QueueSize) + sizeof(uint16_t);
    oserr_t oserr;
    TRACE("__QueueMemoryCreate(queue=%u, size=%u)",
          queue->QueueIndex, queue->QueueSize);

    // Keep the chain length and caller context for each head descriptor here.
    // The device only needs the descriptors, so software pointers stay in
    // these separate arrays.
    queue->FreeList = malloc(sizeof(uint16_t) * queue->QueueSize);
    queue->ChainLengths = calloc(queue->QueueSize, sizeof(uint16_t));
    queue->Allocated = calloc(queue->QueueSize, sizeof(uint8_t));
    queue->Contexts = calloc(queue->QueueSize, sizeof(void*));
    if (queue->FreeList == NULL || queue->ChainLengths == NULL ||
        queue->Allocated == NULL || queue->Contexts == NULL) {
        ERROR("__QueueMemoryCreate failed to allocate queue bookkeeping");
        return OS_EOOM;
    }

    // The descriptor table needs 16-byte alignment, the available ring needs
    // 2-byte alignment, and the used ring needs 4-byte alignment. Allocate them
    // separately so each has its own address in the common configuration.
    oserr = DMAAllocate(queue->Transport->DmaContext,
        &(struct DMARequirements) { .Length = descriptorBytes, .Alignment = 16, .Contiguous = true },
        &queue->DescriptorRegion);
    if (oserr != OS_EOK) {
        ERROR("__QueueMemoryCreate failed to allocate the descriptor table: %u", oserr);
        return oserr;
    }

    oserr = DMAAllocate(queue->Transport->DmaContext,
        &(struct DMARequirements) { .Length = availableBytes, .Alignment = 2, .Contiguous = true },
        &queue->AvailableRegion);
    if (oserr != OS_EOK) {
        ERROR("__QueueMemoryCreate failed to allocate the available ring: %u", oserr);
        return oserr;
    }

    oserr = DMAAllocate(queue->Transport->DmaContext,
        &(struct DMARequirements) { .Length = usedBytes, .Alignment = 4, .Contiguous = true },
        &queue->UsedRegion);
    if (oserr != OS_EOK) {
        ERROR("__QueueMemoryCreate failed to allocate the used ring: %u", oserr);
        return oserr;
    }

    queue->Descriptors = DMABufferData(queue->DescriptorRegion);
    queue->Available   = DMABufferData(queue->AvailableRegion);
    queue->Used        = DMABufferData(queue->UsedRegion);

    // EVENT_IDX adds an event field after each ring array. We always reserve
    // space for these fields so feature negotiation leaves the DMA layout unchanged.
    queue->UsedEvent = &queue->Available->Ring[queue->QueueSize];
    queue->AvailableEvent = (uint16_t*)&queue->Used->Ring[queue->QueueSize];
    queue->FreeDescriptors = queue->QueueSize;

    // FreeList is a stack. Fill it in reverse order so the first chain starts
    // at descriptor zero and uses consecutive descriptors.
    for (uint16_t i = 0; i < queue->QueueSize; i++) {
        queue->FreeList[i] = (uint16_t)(queue->QueueSize - i - 1);
    }
    return OS_EOK;
}

static oserr_t
__ReadQueueLimits(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint16_t              queueIndex,
    _Out_ uint16_t*             maximumSizeOut)
{
    uint64_t value;
    oserr_t  oserr;
    TRACE("__ReadQueueLimits(queue=%u)", queueIndex);

    // QueueSelect must stay unchanged while we read the queue fields. Hold the
    // transport lock so another queue cannot change it between reads.
    spinlock_acquire(&transport->ConfigurationLock);
    oserr = __CommonRead(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, NumQueues),
        sizeof(((VirtioPciCommonConfiguration_t*)0)->NumQueues),
        &value
    );
    if (oserr == OS_EOK && queueIndex >= (uint16_t)value) {
        oserr = OS_ENOENT;
    }
    if (oserr == OS_EOK) {
        oserr = __SelectQueue(transport, queueIndex);
    }
    if (oserr == OS_EOK) {
        oserr = __CommonRead(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueEnable),
            sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueEnable),
            &value
        );
    }

    // Virtio forbids reconfiguring a queue after QueueEnable has been set.
    if (oserr == OS_EOK && value != 0) {
        oserr = OS_EBUSY;
    }
    if (oserr == OS_EOK) {
        oserr = __CommonRead(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueSize),
            sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueSize),
            &value
        );
    }
    spinlock_release(&transport->ConfigurationLock);

    if (oserr == OS_EOK) {
        if (value == 0) {
            return OS_ENOENT;
        }
        *maximumSizeOut = (uint16_t)value;
    }
    return oserr;
}

static oserr_t
__ProgramQueue(
    _InOut_ VirtioSplitQueue_t* queue)
{
    VirtioPciTransport_t* transport = queue->Transport;
    uint64_t              value;
    uint64_t              notifyOffset;
    oserr_t               oserr;
    TRACE("__ProgramQueue(queue=%u, size=%u)",
          queue->QueueIndex, queue->QueueSize);

    // All queues in the PCI function share QueueSelect. Keep the transport
    // locked until the addresses, notification settings and QueueEnable are written.
    spinlock_acquire(&transport->ConfigurationLock);
    oserr = __SelectQueue(transport, queue->QueueIndex);
    if (oserr != OS_EOK) {
        goto exit;
    }
    oserr = __CommonRead(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueEnable),
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueEnable),
        &value
    );
    if (oserr != OS_EOK || value != 0) {
        oserr = oserr == OS_EOK ? OS_EBUSY : oserr;
        goto exit;
    }

    // We use MSI-X table entry zero for every queue. Set and verify the vector
    // before enabling the queue, also when rebuilding it after a reset.
    if (transport->MsixEnabled) {
        oserr = __CommonWrite(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueMsiXVector),
            0,
            sizeof(uint16_t)
        );
        if (oserr != OS_EOK) {
            goto exit;
        }
        
        oserr = __CommonRead(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueMsiXVector),
            sizeof(uint16_t),
            &value
        );
        if (oserr != OS_EOK) {
            goto exit;
        }
        if (value != 0) {
            oserr = OS_EDEVFAULT;
            goto exit;
        }
    }

    // Write the queue size and the device address of each ring allocation.
    // DMAAllocate guarantees that each allocation is one contiguous device range.
    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueSize),
        queue->QueueSize,
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueSize)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueDesc),
        __QueueAddress(queue->DescriptorRegion),
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueDesc)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueDriver),
        __QueueAddress(queue->AvailableRegion),
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueDriver)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueDevice),
        __QueueAddress(queue->UsedRegion),
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueDevice)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    // Multiply QueueNotifyOff to get the byte offset and check that it fits
    // before enabling the queue. Submissions can then notify the device without
    // changing QueueSelect.
    oserr = __CommonRead(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueNotifyOff),
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueNotifyOff),
        &value
    );
    if (oserr != OS_EOK) {
        goto exit;
    }
    notifyOffset = value * transport->NotifyOffsetMultiplier;
    queue->NotificationWidth = sizeof(uint16_t);
    queue->NotifyData = queue->QueueIndex;

    // VIRTIO_F_NOTIFICATION_DATA changes notifications from a 16-bit queue
    // index to a 32-bit value based on the device-provided QueueNotifyData.
    if (transport->DriverFeatures & VIRTIO_F_NOTIFICATION_DATA) {
        if (transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1].Length <
            (offsetof(VirtioPciCommonConfiguration_t, QueueNotifyData) +
             sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueNotifyData))) {
            oserr = OS_ENOTSUPPORTED;
            goto exit;
        }
        oserr = __CommonRead(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueNotifyData),
            sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueNotifyData),
            &value
        );
        if (oserr != OS_EOK) {
            goto exit;
        }
        queue->NotificationWidth = sizeof(uint32_t);
        queue->NotifyData = (uint16_t)value;
    }

    if (notifyOffset > UINT32_MAX ||
        notifyOffset > transport->Regions[VIRTIO_PCI_CAP_NOTIFY_CFG - 1].Length ||
        queue->NotificationWidth >
                transport->Regions[VIRTIO_PCI_CAP_NOTIFY_CFG - 1].Length - notifyOffset) {
        oserr = OS_EOVERFLOW;
        goto exit;
    }
    queue->NotifyOffset = (uint32_t)notifyOffset;

    // The device must see the queue configuration and cleared ring memory
    // before QueueEnable allows it to access the queue.
    dma_wmb();
    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueEnable),
        1,
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueEnable)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }
    queue->Enabled = 1;
    queue->ProgrammedGeneration = transport->ResetGeneration;
        TRACE("__ProgramQueue desc=0x%" PRIx64 ", avail=0x%" PRIx64
            ", used=0x%" PRIx64 ", notify=0x%x/%u, data=0x%x",
            __QueueAddress(queue->DescriptorRegion),
            __QueueAddress(queue->AvailableRegion),
            __QueueAddress(queue->UsedRegion),
            queue->NotifyOffset,
            queue->NotificationWidth,
            queue->NotifyData);

exit:
    spinlock_release(&transport->ConfigurationLock);
    return oserr;
}

static int
__NeedsNotification(
    _In_ VirtioSplitQueue_t* queue,
    _In_ uint16_t            previousIndex,
    _In_ uint16_t            newIndex)
{
    uint16_t eventIndex;
    uint16_t flags;

    // Make the available-ring update visible before reading the device's
    // notification settings. We use them to decide whether to notify it.
    dma_mb();
    if (queue->EventIndex) {
        // With EVENT_IDX, notify when the available index passes the event
        // index set by the device. The unsigned calculation handles 16-bit wraparound.
        ReadVolatileMemory(queue->AvailableEvent, &eventIndex, sizeof(eventIndex));
        return (uint16_t)(newIndex - eventIndex - 1) <
               (uint16_t)(newIndex - previousIndex);
    }

    // Without EVENT_IDX, the used-ring flag disables notifications for all
    // newly available buffers.
    ReadVolatileMemory(&queue->Used->Flags, &flags, sizeof(flags));
    return !(flags & VIRTIO_SPLIT_USED_F_NO_NOTIFY);
}

static oserr_t
__NotifyQueue(
    _In_ VirtioSplitQueue_t* queue,
    _In_ uint16_t            availableIndex)
{
    uint32_t notificationData = queue->NotifyData;

    if (queue->NotificationWidth == sizeof(uint32_t)) {
        // With NOTIFICATION_DATA, bits 31:16 hold the next available index.
        // Bits 15:0 keep the queue notification data supplied by the device.
        notificationData |= (uint32_t)availableIndex << 16;
    }
    return VirtioPciRegionWrite(
            &queue->Transport->Regions[VIRTIO_PCI_CAP_NOTIFY_CFG - 1],
            queue->NotifyOffset,
            notificationData,
            queue->NotificationWidth
    );
}

static oserr_t
__ResetQueue(
    _InOut_ VirtioSplitQueue_t* queue)
{
    VirtioPciTransport_t* transport = queue->Transport;
    uint64_t              value;
    oserr_t               oserr;
    TRACE("__ResetQueue(queue=%u)", queue->QueueIndex);

    // The driver cannot clear QueueEnable. Resetting one queue requires
    // RING_RESET. Otherwise, the caller must reset the whole device before
    // freeing this queue's DMA memory.
    if (!(transport->DriverFeatures & VIRTIO_F_RING_RESET) ||
        transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1].Length <
                sizeof(VirtioPciCommonConfiguration_t)) {
        return OS_EBUSY;
    }

    // QueueReset uses the shared QueueSelect register. Hold the transport lock
    // until the reset finishes so another queue cannot change the selection.
    spinlock_acquire(&transport->ConfigurationLock);
    oserr = __SelectQueue(transport, queue->QueueIndex);
    if (oserr == OS_EOK) {
        oserr = __CommonWrite(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueReset),
            1,
            sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueReset)
        );
    }
    for (int retry = 0; oserr == OS_EOK && retry < VIRTIO_QUEUE_RESET_RETRIES; retry++) {
        oserr = __CommonRead(
            transport,
            offsetof(VirtioPciCommonConfiguration_t, QueueReset),
            sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueReset),
            &value
        );
        if (oserr != OS_EOK || value == 0) {
            break;
        }
        thrd_yield();
    }
    if (oserr == OS_EOK && value != 0) {
        oserr = OS_ETIMEOUT;
    }
    spinlock_release(&transport->ConfigurationLock);

    if (oserr != OS_EOK) {
        ERROR("__ResetQueue failed for queue %u: %u", queue->QueueIndex, oserr);
    }
    return oserr;
}

oserr_t
VirtioSplitQueueCreate(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint16_t              queueIndex,
    _In_  uint16_t              requestedSize,
    _InOut_ VirtioSplitQueue_t** queueOut)
{
    VirtioSplitQueue_t* queue;
    size_t              pageSize;
    uint16_t            maximumSize;
    uint16_t            allocationLimit;
    oserr_t             oserr;
    TRACE("VirtioSplitQueueCreate(queue=%u, requestedSize=%u)",
        queueIndex, requestedSize);

    if (transport == NULL || queueOut == NULL || transport->Device == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*queueOut != NULL || transport->Closing) {
        return OS_EBUSY;
    }
    if (transport->DriverFeatures == 0 ||
        (transport->DriverFeatures & VIRTIO_F_RING_PACKED)) {
        return OS_ENOTSUPPORTED;
    }

    oserr = __ReadQueueLimits(transport, queueIndex, &maximumSize);
    if (oserr != OS_EOK) {
        ERROR("VirtioSplitQueueCreate failed to query queue %u: %u",
              queueIndex, oserr);
        return oserr;
    }

    // We limit each ring allocation to one page. DMAAllocate makes each one
    // a contiguous device address range. The page limit comes from this driver and
    // is separate from the queue size supported by Virtio.
    pageSize = MemoryPageSize();
    allocationLimit = (uint16_t)MIN(pageSize / sizeof(VirtioSplitDescriptor_t), UINT16_MAX);
    maximumSize = MIN(maximumSize, allocationLimit);
    if (requestedSize != 0) {
        maximumSize = MIN(maximumSize, requestedSize);
    }
    if (maximumSize == 0) {
        return OS_ENOTSUPPORTED;
    }

    // Split queues require a power-of-two size. Round down to stay within the
    // requested size and the device's maximum.
    maximumSize = __FloorPowerOfTwo(maximumSize);
    if ((sizeof(VirtioSplitDescriptor_t) * maximumSize) > pageSize ||
        (offsetof(VirtioSplitAvailableRing_t, Ring) +
         (sizeof(uint16_t) * maximumSize) + sizeof(uint16_t)) > pageSize ||
        (offsetof(VirtioSplitUsedRing_t, Ring) +
         (sizeof(VirtioSplitUsedElement_t) * maximumSize) + sizeof(uint16_t)) > pageSize) {
        ERROR("VirtioSplitQueueCreate cannot fit queue %u in contiguous pages",
              queueIndex);
        return OS_ENOTSUPPORTED;
    }

    queue = calloc(1, sizeof(VirtioSplitQueue_t));
    if (queue == NULL) {
        return OS_EOOM;
    }
    queue->Transport  = transport;
    queue->QueueIndex = queueIndex;
    queue->QueueSize  = maximumSize;
    queue->EventIndex = (transport->DriverFeatures & VIRTIO_F_RING_EVENT_IDX) != 0;
    spinlock_init(&queue->Lock);
    
    // Count the queue before allocating memory. If setup and cleanup both fail,
    // the transport must stay open until this queue is freed.
    spinlock_acquire(&transport->ConfigurationLock);
    transport->ActiveQueues++;
    spinlock_release(&transport->ConfigurationLock);

    oserr = __QueueMemoryCreate(queue);
    if (oserr != OS_EOK) {
        ERROR("VirtioSplitQueueCreate failed to allocate queue %u: %u",
              queueIndex, oserr);
        if (VirtioSplitQueueDestroy(queue) != OS_EOK) {
            *queueOut = queue;
        }
        return oserr;
    }
    
    oserr = __ProgramQueue(queue);
    if (oserr != OS_EOK) {
        ERROR("VirtioSplitQueueCreate failed to program queue %u: %u",
              queueIndex, oserr);
        if (VirtioSplitQueueDestroy(queue) != OS_EOK) {
            *queueOut = queue;
        }
        return oserr;
    }

    *queueOut = queue;
    return OS_EOK;
}

oserr_t
VirtioSplitQueueDestroy(
    _In_ VirtioSplitQueue_t* queue)
{
    VirtioPciTransport_t* transport;
    oserr_t               oserr = OS_EOK;

    if (queue == NULL) {
        return OS_EOK;
    }
    TRACE("VirtioSplitQueueDestroy(queue=%u)", queue->QueueIndex);
    transport = queue->Transport;

    spinlock_acquire(&queue->Lock);
    if (queue->Enabled &&
        queue->ProgrammedGeneration == transport->ResetGeneration) {
        // Reset can yield. Block new submissions before releasing the queue lock.
        queue->Quiescing = 1;
        spinlock_release(&queue->Lock);
        oserr = __ResetQueue(queue);
        if (oserr != OS_EOK) {
            return oserr;
        }
        spinlock_acquire(&queue->Lock);
    }
    queue->Enabled = 0;
    queue->Quiescing = 1;

    // Reset stops DMA. The caller must still collect each pending request
    // through VirtioSplitQueueAbort before we can free its context entry.
    if (queue->InFlight != 0) {
        spinlock_release(&queue->Lock);
        return OS_EBUSY;
    }
    spinlock_release(&queue->Lock);

    oserr = __QueueMemoryDestroy(queue);
    if (oserr != OS_EOK) {
        WARNING("Virtio queue %u DMA cleanup deferred: %u", queue->QueueIndex, oserr);
        return oserr;
    }
    
    spinlock_acquire(&transport->ConfigurationLock);
    if (transport->ActiveQueues > 0) {
        transport->ActiveQueues--;
    }
    spinlock_release(&transport->ConfigurationLock);
    free(queue);
    return OS_EOK;
}

oserr_t
VirtioSplitQueueSubmit(
    _In_      VirtioSplitQueue_t*        queue,
    _In_      const VirtioQueueBuffer_t* buffers,
    _In_      uint16_t                   bufferCount,
    _In_Opt_  void*                      context,
    _Out_Opt_ uint16_t*                  headDescriptorOut)
{
    uint16_t previousDescriptor = 0;
    uint16_t headDescriptor = 0;
    uint16_t previousIndex;
    uint16_t newIndex;
    oserr_t  oserr = OS_EOK;

    if (queue == NULL || buffers == NULL || bufferCount == 0 ||
        bufferCount > queue->QueueSize) {
        return OS_EINVALPARAMS;
    }
    for (uint16_t i = 0; i < bufferCount; i++) {
        if (buffers[i].Length == 0 ||
            (buffers[i].Flags & ~VIRTIO_SPLIT_DESC_F_WRITE) != 0) {
            return OS_EINVALPARAMS;
        }
    }

    spinlock_acquire(&queue->Lock);

    // The device loses its queue configuration on reset. Check the generation
    // so we reject submissions even if teardown has not cleared Enabled yet.
    if (!queue->Enabled || queue->Quiescing || queue->Faulted ||
        queue->NotificationFailed ||
        queue->ProgrammedGeneration != queue->Transport->ResetGeneration) {
        oserr = OS_EDEVFAULT;
        goto exit;
    }
    if (queue->FreeDescriptors < bufferCount) {
        oserr = OS_EBUSY;
        goto exit;
    }

    // Build the chain from free descriptors. We set NEXT here so callers cannot
    // create loops or link descriptors that belong to another request.
    for (uint16_t i = 0; i < bufferCount; i++) {
        uint16_t descriptorIndex = queue->FreeList[--queue->FreeDescriptors];
        VirtioSplitDescriptor_t* descriptor = &queue->Descriptors[descriptorIndex];

        queue->Allocated[descriptorIndex] = 1;
        descriptor->Address = buffers[i].Address;
        descriptor->Length  = buffers[i].Length;
        descriptor->Flags   = buffers[i].Flags;
        descriptor->Next    = 0;

        if (i == 0) {
            headDescriptor = descriptorIndex;
        }
        else {
            queue->Descriptors[previousDescriptor].Flags |= VIRTIO_SPLIT_DESC_F_NEXT;
            queue->Descriptors[previousDescriptor].Next = descriptorIndex;
        }
        previousDescriptor = descriptorIndex;
    }

    queue->ChainLengths[headDescriptor] = bufferCount;
    queue->Contexts[headDescriptor] = context;
    previousIndex = queue->AvailableIndex;
    queue->Available->Ring[previousIndex % queue->QueueSize] = headDescriptor;

    // The descriptors and available-ring entry must be visible to the device
    // before RingIndex tells it that the chain is ready.
    dma_wmb();
    newIndex = (uint16_t)(previousIndex + 1);
    WriteVolatileMemory(&queue->Available->RingIndex, &newIndex, sizeof(newIndex));
    queue->AvailableIndex = newIndex;
    queue->InFlight++;
    queue->Submitted++;
    TRACE("VirtioSplitQueueSubmit queue=%u, head=%u, descriptors=%u, avail=%u",
          queue->QueueIndex, headDescriptor, bufferCount, newIndex);
    if (headDescriptorOut != NULL) {
        *headDescriptorOut = headDescriptor;
    }

    if (__NeedsNotification(queue, previousIndex, newIndex)) {
        oserr = __NotifyQueue(queue, newIndex);
        if (oserr != OS_EOK) {
            // The device may already be using this chain, so we must leave it
            // queued. Report it as in progress and block submissions until
            // the driver resets or recovers the queue.
            queue->NotificationFailed = 1;
            oserr = OS_EINPROGRESS;
        }
        else {
            TRACE("VirtioSplitQueueSubmit notified queue=%u", queue->QueueIndex);
        }
    }

exit:
    spinlock_release(&queue->Lock);
    return oserr;
}

static oserr_t
__ValidateCompletedChain(
    _In_ VirtioSplitQueue_t* queue,
    _In_ uint16_t            headDescriptor,
    _In_ uint16_t            chainLength)
{
    uint16_t descriptorIndex = headDescriptor;

    // The device writes the used ring, while we build the descriptor chains.
    // Check the saved chain before following its links and freeing descriptors.
    // Invalid links or completions must return a protocol error.
    for (uint16_t i = 0; i < chainLength; i++) {
        VirtioSplitDescriptor_t* descriptor;

        if (descriptorIndex >= queue->QueueSize ||
            !queue->Allocated[descriptorIndex]) {
            return OS_EPROTOCOL;
        }
        descriptor = &queue->Descriptors[descriptorIndex];
        if (descriptor->Flags & ~(VIRTIO_SPLIT_DESC_F_NEXT |
                                  VIRTIO_SPLIT_DESC_F_WRITE)) {
            return OS_EPROTOCOL;
        }
        if (i + 1 == chainLength) {
            if (descriptor->Flags & VIRTIO_SPLIT_DESC_F_NEXT) {
                return OS_EPROTOCOL;
            }
        }
        else {
            if (!(descriptor->Flags & VIRTIO_SPLIT_DESC_F_NEXT)) {
                return OS_EPROTOCOL;
            }
            descriptorIndex = descriptor->Next;
        }
    }
    return OS_EOK;
}

static void
__ReclaimCompletedChain(
    _InOut_ VirtioSplitQueue_t* queue,
    _In_    uint16_t            headDescriptor,
    _In_    uint16_t            chainLength)
{
    uint16_t descriptorIndex = headDescriptor;

    for (uint16_t i = 0; i < chainLength; i++) {
        VirtioSplitDescriptor_t* descriptor = &queue->Descriptors[descriptorIndex];
        uint16_t nextDescriptor = descriptor->Next;

        memset(descriptor, 0, sizeof(VirtioSplitDescriptor_t));
        queue->Allocated[descriptorIndex] = 0;
        queue->FreeList[queue->FreeDescriptors++] = descriptorIndex;
        descriptorIndex = nextDescriptor;
    }
    queue->ChainLengths[headDescriptor] = 0;
    queue->Contexts[headDescriptor] = NULL;
}

oserr_t
VirtioSplitQueuePoll(
    _In_  VirtioSplitQueue_t*      queue,
    _Out_ VirtioQueueCompletion_t* completionOut)
{
    VirtioSplitUsedElement_t element;
    uint16_t                 deviceUsedIndex;
    uint16_t                 chainLength;
    uint16_t                 headDescriptor;
    oserr_t                  oserr = OS_EOK;

    if (queue == NULL || completionOut == NULL) {
        return OS_EINVALPARAMS;
    }

    spinlock_acquire(&queue->Lock);

    // Poll only while the device still has this queue's configuration. After
    // reset, the caller must collect pending contexts through VirtioSplitQueueAbort.
    if (!queue->Enabled || queue->Quiescing || queue->Faulted ||
        queue->ProgrammedGeneration != queue->Transport->ResetGeneration) {
        oserr = OS_EDEVFAULT;
        goto exit;
    }

    ReadVolatileMemory(&queue->Used->RingIndex, &deviceUsedIndex, sizeof(deviceUsedIndex));
    if (deviceUsedIndex == queue->UsedIndex) {
        oserr = OS_ENOENT;
        goto exit;
    }

    // The device can be at most one full ring ahead of the driver. A larger
    // difference means the used-ring state is corrupt.
    if ((uint16_t)(deviceUsedIndex - queue->UsedIndex) > queue->QueueSize) {
        queue->Faulted = 1;
        oserr = OS_EPROTOCOL;
        goto exit;
    }

    // Read the used element after the updated RingIndex so we get the
    // completion data that the device wrote before updating the index.
    dma_rmb();
    ReadVolatileMemory(
            &queue->Used->Ring[queue->UsedIndex % queue->QueueSize],
            &element,
            sizeof(element)
    );
    queue->UsedIndex++;
    if (queue->EventIndex) {
        // Finish prior memory writes before telling the device which used
        // index should trigger the next interrupt.
        dma_wmb();
        WriteVolatileMemory(queue->UsedEvent, &queue->UsedIndex, sizeof(queue->UsedIndex));
    }

    if (element.DescriptorId >= queue->QueueSize) {
        queue->Faulted = 1;
        oserr = OS_EPROTOCOL;
        goto exit;
    }
    headDescriptor = (uint16_t)element.DescriptorId;
    chainLength = queue->ChainLengths[headDescriptor];
    if (chainLength == 0 || queue->InFlight == 0) {
        queue->Faulted = 1;
        oserr = OS_EPROTOCOL;
        goto exit;
    }

    oserr = __ValidateCompletedChain(queue, headDescriptor, chainLength);
    if (oserr != OS_EOK) {
        queue->Faulted = 1;
        goto exit;
    }

    completionOut->Context = queue->Contexts[headDescriptor];
    completionOut->Length = element.Length;
    completionOut->HeadDescriptor = headDescriptor;
    __ReclaimCompletedChain(queue, headDescriptor, chainLength);
    queue->InFlight--;
    queue->Completed++;
        TRACE("VirtioSplitQueuePoll queue=%u, head=%u, length=%u",
            queue->QueueIndex, headDescriptor, element.Length);

exit:
    spinlock_release(&queue->Lock);
    return oserr;
}

oserr_t
VirtioSplitQueueAbort(
    _In_  VirtioSplitQueue_t*      queue,
    _Out_ VirtioQueueCompletion_t* completionOut)
{
    uint16_t headDescriptor;
    uint16_t chainLength;
    oserr_t  oserr = OS_ENOENT;

    if (queue == NULL || completionOut == NULL) {
        return OS_EINVALPARAMS;
    }

    spinlock_acquire(&queue->Lock);

    // The queue must be reset before we free a submitted chain. The device
    // must have stopped using its descriptors and ring memory.
    if (queue->Enabled &&
        queue->ProgrammedGeneration == queue->Transport->ResetGeneration) {
        oserr = OS_EBUSY;
        goto exit;
    }

    for (headDescriptor = 0; headDescriptor < queue->QueueSize; headDescriptor++) {
        chainLength = queue->ChainLengths[headDescriptor];
        if (chainLength != 0) {
            oserr = __ValidateCompletedChain(queue, headDescriptor, chainLength);
            if (oserr != OS_EOK) {
                queue->Faulted = 1;
                goto exit;
            }

            completionOut->Context = queue->Contexts[headDescriptor];
            completionOut->Length = 0;
            completionOut->HeadDescriptor = headDescriptor;
            __ReclaimCompletedChain(queue, headDescriptor, chainLength);
            queue->InFlight--;
            oserr = OS_EOK;
            break;
        }
    }

exit:
    spinlock_release(&queue->Lock);
    return oserr;
}

oserr_t
VirtioSplitQueueGetStats(
    _In_  VirtioSplitQueue_t* queue,
    _Out_ VirtioQueueStats_t* statsOut)
{
    if (queue == NULL || statsOut == NULL) {
        return OS_EINVALPARAMS;
    }

    spinlock_acquire(&queue->Lock);
    statsOut->Submitted       = queue->Submitted;
    statsOut->Completed       = queue->Completed;
    statsOut->QueueSize       = queue->QueueSize;
    statsOut->FreeDescriptors = queue->FreeDescriptors;
    statsOut->InFlight        = queue->InFlight;
    statsOut->AvailableIndex  = queue->AvailableIndex;
    statsOut->UsedIndex       = queue->UsedIndex;
    spinlock_release(&queue->Lock);
    return OS_EOK;
}
