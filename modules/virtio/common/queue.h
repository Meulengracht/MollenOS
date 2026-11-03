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

#ifndef __VIRTIO_QUEUE_H__
#define __VIRTIO_QUEUE_H__

#define VIRTIO_QUEUE_RESET_RETRIES 1000

/**
 * @brief Split virtqueue memory, descriptors, and queue state.
 *
 * DescriptorRegion, AvailableRegion and UsedRegion must stay allocated while
 * the queue is enabled. The device can access them at any time. Descriptors,
 * Available and Used point into these allocations.
 *
 * FreeList, ChainLengths, Allocated and Contexts are used only by the driver
 * to track descriptor chains and find the caller when a chain completes.
 *
 * Lock protects queue state and descriptor allocation. Transport configuration
 * uses Transport->ConfigurationLock.
 */
struct VirtioSplitQueue {
    VirtioPciTransport_t*       Transport;       // PCI transport for this queue.

    // Device-visible split-ring memory.
    struct DMABuffer*           DescriptorRegion; // Descriptor table allocation.
    struct DMABuffer*           AvailableRegion;  // Available ring written by the driver.
    struct DMABuffer*           UsedRegion;       // Used ring written by the device.
    VirtioSplitDescriptor_t*    Descriptors;      // Mapped descriptor table.
    VirtioSplitAvailableRing_t* Available;        // Mapped available-ring header and entries.
    VirtioSplitUsedRing_t*      Used;             // Mapped used-ring header and entries.
    uint16_t*                   UsedEvent;         // Optional event index following Available.
    uint16_t*                   AvailableEvent;    // Optional event index following Used.

    // Descriptor tracking used only by the driver.
    uint16_t*                   FreeList;          // Stack of free descriptor indices.
    uint16_t*                   ChainLengths;      // Chain length indexed by submitted head.
    uint8_t*                    Allocated;         // Nonzero while a descriptor belongs to a chain.
    void**                      Contexts;          // Caller context indexed by submitted head.

    // Queue counters and notification settings.
    uint64_t                    Submitted;         // Number of chains published to the device.
    uint64_t                    Completed;         // Number of chains reclaimed from the used ring.
    uint32_t                    ProgrammedGeneration; // Transport reset generation at enable time.
    uint32_t                    NotifyOffset;      // Byte offset within the notify capability region.
    uint32_t                    NotifyData;        // Value written when notifying this queue.
    uint16_t                    QueueIndex;         // Queue number selected in common configuration.
    uint16_t                    QueueSize;          // Descriptor and ring-entry count.
    uint16_t                    FreeDescriptors;   // Number of entries in FreeList.
    uint16_t                    AvailableIndex;    // Next available-ring index, wrapping at 16 bits.
    uint16_t                    UsedIndex;          // Next used-ring index, wrapping at 16 bits.
    uint16_t                    InFlight;           // Published chains not yet completed or aborted.
    uint8_t                     NotificationWidth;  // Notify write width in bytes.

    // Queue status and errors.
    uint8_t                     Enabled;            // Device may access the programmed ring memory.
    uint8_t                     Quiescing;          // New submissions are blocked during teardown.
    uint8_t                     Faulted;            // Queue metadata or device response was invalid.
    uint8_t                     NotificationFailed; // Notification failed after submitting a chain.
    uint8_t                     EventIndex;          // VIRTIO_F_RING_EVENT_IDX was negotiated.
    spinlock_t                  Lock;                // Protects every mutable field above.
};


#endif //__VIRTIO_QUEUE_H__
