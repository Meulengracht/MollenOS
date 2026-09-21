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
#include <os/handle.h>
#include <os/memory.h>
#include <os/shm.h>
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

static void
__DmaRegionDestroy(
    _In_ VirtioDmaRegion_t* region)
{
    if (region->Handle.ID != UUID_INVALID) {
        OSHandleDestroy(&region->Handle);
    }
    free(region->SgTable.Entries);
    memset(region, 0, sizeof(VirtioDmaRegion_t));
}

static oserr_t
__DmaRegionCreate(
    _In_  size_t             length,
    _In_  size_t             alignment,
    _Out_ VirtioDmaRegion_t* region)
{
    oserr_t oserr;
    TRACE("__DmaRegionCreate(length=%" PRIuIN ", alignment=%" PRIuIN ")",
          length, alignment);

    if (length == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return OS_EINVALPARAMS;
    }

    // Virtio 1.x exposes 64-bit queue addresses, but low physical memory keeps
    // the queue usable by both 32-bit and 64-bit Vali targets without an IOMMU.
    memset(region, 0, sizeof(VirtioDmaRegion_t));
    oserr = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
            .Conformity = OSMEMORYCONFORMITY_BITS32,
            .Size = length,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
        },
        &region->Handle
    );
    if (oserr != OS_EOK) {
        ERROR("__DmaRegionCreate failed to allocate DMA memory: %u", oserr);
        return oserr;
    }

    // The common queue representation programs one physical base address for
    // each split-ring component. Consequently, each allocation must resolve to
    // one contiguous SG entry covering the complete region; accepting a
    // fragmented table here would make the device walk into unrelated memory.
    oserr = SHMGetSGTable(&region->Handle, &region->SgTable, -1);
    if (oserr != OS_EOK) {
        ERROR("__DmaRegionCreate failed to retrieve the SG table: %u", oserr);
        __DmaRegionDestroy(region);
        return oserr;
    }

    // Verify both the geometry promised to the device and the alignment
    // required by the specific split-ring structure being allocated.
    if (region->SgTable.Count != 1 ||
        region->SgTable.Entries[0].Length < length ||
        (region->SgTable.Entries[0].Address & (alignment - 1)) != 0) {
        ERROR("__DmaRegionCreate received incompatible DMA geometry");
        __DmaRegionDestroy(region);
        return OS_ENOTSUPPORTED;
    }

    region->Buffer          = SHMBuffer(&region->Handle);
    region->PhysicalAddress = region->SgTable.Entries[0].Address;
    region->Length          = length;
    if (region->Buffer == NULL) {
        ERROR("__DmaRegionCreate failed to map the DMA allocation");
        __DmaRegionDestroy(region);
        return OS_EOOM;
    }
    return OS_EOK;
}

static void
__QueueMemoryDestroy(
    _In_ VirtioSplitQueue_t* queue)
{
    __DmaRegionDestroy(&queue->UsedRegion);
    __DmaRegionDestroy(&queue->AvailableRegion);
    __DmaRegionDestroy(&queue->DescriptorRegion);
    free(queue->Contexts);
    free(queue->Allocated);
    free(queue->ChainLengths);
    free(queue->FreeList);
    free(queue);
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

    // These arrays are driver-only bookkeeping. They keep device-visible
    // descriptors free of software pointers and associate each submitted head
    // descriptor with its complete chain and caller context.
    queue->FreeList = malloc(sizeof(uint16_t) * queue->QueueSize);
    queue->ChainLengths = calloc(queue->QueueSize, sizeof(uint16_t));
    queue->Allocated = calloc(queue->QueueSize, sizeof(uint8_t));
    queue->Contexts = calloc(queue->QueueSize, sizeof(void*));
    if (queue->FreeList == NULL || queue->ChainLengths == NULL ||
        queue->Allocated == NULL || queue->Contexts == NULL) {
        ERROR("__QueueMemoryCreate failed to allocate queue bookkeeping");
        return OS_EOOM;
    }

    // Split virtqueues have independent alignment requirements: 16 bytes for
    // descriptors, 2 bytes for the available ring, and 4 bytes for the used
    // ring. Separate allocations also let each component be programmed through
    // its corresponding common-configuration address field.
    oserr = __DmaRegionCreate(descriptorBytes, 16, &queue->DescriptorRegion);
    if (oserr != OS_EOK) {
        ERROR("__QueueMemoryCreate failed to allocate the descriptor table: %u", oserr);
        return oserr;
    }
    oserr = __DmaRegionCreate(availableBytes, 2, &queue->AvailableRegion);
    if (oserr != OS_EOK) {
        ERROR("__QueueMemoryCreate failed to allocate the available ring: %u", oserr);
        return oserr;
    }
    oserr = __DmaRegionCreate(usedBytes, 4, &queue->UsedRegion);
    if (oserr != OS_EOK) {
        ERROR("__QueueMemoryCreate failed to allocate the used ring: %u", oserr);
        return oserr;
    }

    queue->Descriptors = queue->DescriptorRegion.Buffer;
    queue->Available   = queue->AvailableRegion.Buffer;
    queue->Used        = queue->UsedRegion.Buffer;

    // When EVENT_IDX is negotiated, the optional event fields immediately
    // follow the variable-length ring arrays. Space is reserved unconditionally
    // so feature negotiation does not change the DMA layout.
    queue->UsedEvent = &queue->Available->Ring[queue->QueueSize];
    queue->AvailableEvent = (uint16_t*)&queue->Used->Ring[queue->QueueSize];
    queue->FreeDescriptors = queue->QueueSize;

    // FreeList is a stack. Reverse initialization makes the first allocation
    // return descriptor zero and keeps early chains deterministic and compact.
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

    // QueueSelect controls which queue all subsequent queue fields describe.
    // Hold the transport lock across the complete select/read sequence so a
    // second queue cannot redirect those accesses midway through the query.
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

    // QueueSelect is shared by every queue in the function. Keep it stable
    // until all addresses, notification metadata, and QueueEnable are written.
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

    // Publish the chosen size and the three independent split-ring physical
    // bases. The DMA allocator has already guaranteed each base describes the
    // entire corresponding component without SG discontinuities.
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
        queue->DescriptorRegion.PhysicalAddress,
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueDesc)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueDriver),
        queue->AvailableRegion.PhysicalAddress,
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueDriver)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = __CommonWrite(
        transport,
        offsetof(VirtioPciCommonConfiguration_t, QueueDevice),
        queue->UsedRegion.PhysicalAddress,
        sizeof(((VirtioPciCommonConfiguration_t*)0)->QueueDevice)
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    // QueueNotifyOff is expressed in multiplier units, not bytes. Resolve and
    // bounds-check the final offset before the queue is made visible to the
    // device so later submissions can notify without touching QueueSelect.
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

    // All queue configuration and zeroed ring memory must be globally visible
    // before QueueEnable transfers asynchronous ownership to the device.
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
    transport->ActiveQueues++;
        TRACE("__ProgramQueue desc=0x%" PRIxIN ", avail=0x%" PRIxIN
            ", used=0x%" PRIxIN ", notify=0x%x/%u, data=0x%x",
            queue->DescriptorRegion.PhysicalAddress,
            queue->AvailableRegion.PhysicalAddress,
            queue->UsedRegion.PhysicalAddress,
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

    // The suppression fields are device-owned. Order their reads after the
    // available-ring publication before deciding whether a doorbell is needed.
    dma_mb();
    if (queue->EventIndex) {
        // EVENT_IDX requests notification only when this publication crosses
        // the device-selected event index. Unsigned arithmetic intentionally
        // preserves the Virtio 16-bit wraparound semantics.
        ReadVolatileMemory(queue->AvailableEvent, &eventIndex, sizeof(eventIndex));
        return (uint16_t)(newIndex - eventIndex - 1) <
               (uint16_t)(newIndex - previousIndex);
    }

    // Without EVENT_IDX, the used-ring flag provides coarse notification
    // suppression for all newly available buffers.
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
        // With NOTIFICATION_DATA, bits 31:16 carry the next split available
        // index while the device-provided queue notification data stays low.
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

    // QueueEnable cannot be cleared by the driver. Individual queue teardown
    // is safe only when RING_RESET is negotiated; otherwise the caller must
    // reset the complete device before reclaiming this queue's DMA memory.
    if (!(transport->DriverFeatures & VIRTIO_F_RING_RESET) ||
        transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1].Length <
                sizeof(VirtioPciCommonConfiguration_t)) {
        return OS_EBUSY;
    }

    // QueueReset, like all per-queue common fields, is selected through the
    // shared QueueSelect register and must remain serialized until completion.
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
    _Out_ VirtioSplitQueue_t**  queueOut)
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
    *queueOut = NULL;
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

    // This implementation requires one contiguous SG entry per split-ring
    // component. Limiting every component to one page makes that geometry
    // deterministic with the current SHM allocator. This is an implementation
    // policy, not a Virtio queue-size limit.
    pageSize = MemoryPageSize();
    allocationLimit = (uint16_t)MIN(pageSize / sizeof(VirtioSplitDescriptor_t), UINT16_MAX);
    maximumSize = MIN(maximumSize, allocationLimit);
    if (requestedSize != 0) {
        maximumSize = MIN(maximumSize, requestedSize);
    }
    if (maximumSize == 0) {
        return OS_ENOTSUPPORTED;
    }

    // Split queues require a power-of-two size. Round down so a request or
    // device maximum that is not a power of two never exceeds either limit.
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

    oserr = __QueueMemoryCreate(queue);
    if (oserr != OS_EOK) {
        ERROR("VirtioSplitQueueCreate failed to allocate queue %u: %u",
              queueIndex, oserr);
        __QueueMemoryDestroy(queue);
        return oserr;
    }
    
    oserr = __ProgramQueue(queue);
    if (oserr != OS_EOK) {
        ERROR("VirtioSplitQueueCreate failed to program queue %u: %u",
              queueIndex, oserr);
        __QueueMemoryDestroy(queue);
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
        // Block new submissions before releasing the queue lock to perform the
        // potentially yielding hardware reset.
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

    // A reset stops DMA but does not complete caller-owned requests. Preserve
    // their contexts until VirtioSplitQueueAbort has returned each one.
    if (queue->InFlight != 0) {
        spinlock_release(&queue->Lock);
        return OS_EBUSY;
    }
    spinlock_release(&queue->Lock);

    spinlock_acquire(&transport->ConfigurationLock);
    if (transport->ActiveQueues > 0) {
        transport->ActiveQueues--;
    }
    spinlock_release(&transport->ConfigurationLock);
    __QueueMemoryDestroy(queue);
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

    // A transport reset invalidates the device-side queue configuration. The
    // generation check prevents submission through a stale queue object even
    // if its local Enabled flag has not yet been cleared by teardown.
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

    // Pop descriptors from the private free stack and construct a direct
    // chain. NEXT is generated here so callers cannot create cycles or refer
    // to descriptors owned by another request.
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

    // The device treats RingIndex as publication. Ensure descriptor contents
    // and the available-ring entry reach device-visible memory first.
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
            // The chain is already published and cannot be rolled back safely.
            // Report it as in progress and prevent further submissions until
            // the owning driver resets or otherwise recovers the queue.
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

    // The used ring is device-controlled, but descriptor chains are not.
    // Validate the saved chain before using its links to reclaim bookkeeping;
    // this turns corruption or an invalid completion into a protocol fault.
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

    // Completion polling is valid only for the device generation to which the
    // ring addresses were programmed. After a reset, outstanding contexts must
    // instead be recovered through VirtioSplitQueueAbort.
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

    // The device cannot legitimately advance more than one complete ring ahead
    // of the driver. A larger delta means used-ring state was corrupted.
    if ((uint16_t)(deviceUsedIndex - queue->UsedIndex) > queue->QueueSize) {
        queue->Faulted = 1;
        oserr = OS_EPROTOCOL;
        goto exit;
    }

    // RingIndex is the device's publication point. Once observed, order the
    // used-element read after it so the completion payload cannot be stale.
    dma_rmb();
    ReadVolatileMemory(
            &queue->Used->Ring[queue->UsedIndex % queue->QueueSize],
            &element,
            sizeof(element)
    );
    queue->UsedIndex++;
    if (queue->EventIndex) {
        // Tell the device which future used index should trigger an interrupt.
        // Publish prior completion processing before exposing the new event.
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

    // Reclaiming a published chain is safe only after queue reset has revoked
    // device ownership of the descriptor and ring memory.
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
