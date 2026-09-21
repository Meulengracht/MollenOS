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
 * @brief Represents one physically contiguous device-visible memory region.
 *
 * The shared-memory handle owns the allocation and its process mapping. The SG
 * table is retained so its entries can be released during teardown. Queue
 * programming uses PhysicalAddress, which is valid only when the SG table
 * contains exactly one entry covering Length bytes.
 */
typedef struct VirtioDmaRegion {
    OSHandle_t   Handle;          // Shared-memory handle owning the DMA allocation.
    SHMSGTable_t SgTable;         // Physical scatter-gather description of Handle.
    void*        Buffer;          // Process mapping of the allocated memory.
    uintptr_t    PhysicalAddress; // Device-visible base address of the single SG entry.
    size_t       Length;          // Number of bytes available from Buffer and PhysicalAddress.
} VirtioDmaRegion_t;

/**
 * @brief Contains the device-visible and driver-owned state of a split virtqueue.
 *
 * DescriptorRegion, AvailableRegion, and UsedRegion remain allocated while the
 * queue is enabled because the device may access them asynchronously. The raw
 * ring pointers address those mappings, while the free list, chain lengths,
 * allocation map, and contexts are private ownership metadata used to associate
 * a returned descriptor head with its caller.
 *
 * Lock protects all mutable queue state and descriptor ownership. Transport
 * configuration is separately serialized by Transport->ConfigurationLock.
 */
struct VirtioSplitQueue {
    VirtioPciTransport_t*       Transport;       // PCI transport that owns the queue.

    /** Device-visible split-ring memory. */
    VirtioDmaRegion_t           DescriptorRegion; // Descriptor table allocation.
    VirtioDmaRegion_t           AvailableRegion;  // Driver-owned available-ring allocation.
    VirtioDmaRegion_t           UsedRegion;       // Device-owned used-ring allocation.
    VirtioSplitDescriptor_t*    Descriptors;      // Mapped descriptor table.
    VirtioSplitAvailableRing_t* Available;        // Mapped available-ring header and entries.
    VirtioSplitUsedRing_t*      Used;             // Mapped used-ring header and entries.
    uint16_t*                   UsedEvent;         // Optional event index following Available.
    uint16_t*                   AvailableEvent;    // Optional event index following Used.

    /** Driver-only descriptor ownership metadata. */
    uint16_t*                   FreeList;          // Stack of currently free descriptor indices.
    uint16_t*                   ChainLengths;      // Chain length indexed by submitted head.
    uint8_t*                    Allocated;         // Nonzero while a descriptor belongs to a chain.
    void**                      Contexts;          // Caller context indexed by submitted head.

    /** Queue accounting and programmed notification state. */
    uint64_t                    Submitted;         // Number of chains published to the device.
    uint64_t                    Completed;         // Number of chains reclaimed from the used ring.
    uint32_t                    ProgrammedGeneration; // Transport reset generation at enable time.
    uint32_t                    NotifyOffset;      // Byte offset within the notify capability region.
    uint32_t                    NotifyData;        // Value written when notifying this queue.
    uint16_t                    QueueIndex;         // Queue number selected in common configuration.
    uint16_t                    QueueSize;          // Descriptor and ring-entry count.
    uint16_t                    FreeDescriptors;   // Number of entries currently in FreeList.
    uint16_t                    AvailableIndex;    // Driver's next monotonically wrapping avail index.
    uint16_t                    UsedIndex;          // Driver's next monotonically wrapping used index.
    uint16_t                    InFlight;           // Published chains not yet completed or aborted.
    uint8_t                     NotificationWidth;  // Notify write width in bytes.

    /** Queue lifecycle and failure state. */
    uint8_t                     Enabled;            // Device may access the programmed ring memory.
    uint8_t                     Quiescing;          // New submissions are blocked during teardown.
    uint8_t                     Faulted;            // Queue metadata or device response was invalid.
    uint8_t                     NotificationFailed; // A published chain could not be notified.
    uint8_t                     EventIndex;          // VIRTIO_F_RING_EVENT_IDX was negotiated.
    spinlock_t                  Lock;                // Protects every mutable field above.
};


#endif //__VIRTIO_QUEUE_H__
