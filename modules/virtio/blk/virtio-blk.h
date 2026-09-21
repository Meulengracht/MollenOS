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
 * Flush, discard, write-zeroes, multi-queue operation, and packed rings are not
 * negotiated yet
 *
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#ifndef __VIRTIO_BLOCK_H__
#define __VIRTIO_BLOCK_H__

#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <ddk/storage.h>
#include <ds/list.h>
#include <gracht/server.h>
#include <os/shm.h>
#include <stdatomic.h>
#include <virtio/virtio.h>

/** Device configuration exposes the maximum size of one data segment. */
#define VIRTIO_BLK_F_SIZE_MAX (1ULL << 1)
/** Device configuration exposes the maximum number of data segments. */
#define VIRTIO_BLK_F_SEG_MAX  (1ULL << 2)
/** The block device is read-only and must reject write requests. */
#define VIRTIO_BLK_F_RO       (1ULL << 5)
/** Device configuration exposes its logical block size. */
#define VIRTIO_BLK_F_BLK_SIZE (1ULL << 6)

/** Read data from the device into device-writable guest buffers. */
#define VIRTIO_BLK_T_IN  0
/** Write data from device-readable guest buffers to the device. */
#define VIRTIO_BLK_T_OUT 1

/** The request completed successfully. */
#define VIRTIO_BLK_S_OK     0
/** The request failed because of a device or media I/O error. */
#define VIRTIO_BLK_S_IOERR  1
/** The device does not support the requested operation. */
#define VIRTIO_BLK_S_UNSUPP 2

/** Virtio block request sectors are always expressed in 512-byte units. */
#define VIRTIO_BLK_SECTOR_SIZE 512
/** Preferred upper bound for the single split request queue. */
#define VIRTIO_BLK_QUEUE_SIZE  128

/**
 * @brief Device-readable header at the start of every block request chain.
 *
 * The fields use the little-endian Virtio wire format. Sector is measured in
 * VIRTIO_BLK_SECTOR_SIZE units, regardless of the negotiated logical block
 * size. A device-writable status byte terminates the descriptor chain.
 */
PACKED_TYPESTRUCT(VirtioBlkRequestHeader, {
    uint32_t Type;      /**< VIRTIO_BLK_T_* operation requested by the driver. */
    uint32_t Reserved;  /**< Reserved by Virtio; the driver must write zero. */
    uint64_t Sector;    /**< First 512-byte sector accessed by the request. */
});

/**
 * @brief State shared with the fast interrupt handler.
 *
 * The ISR capability offset is relative to IoSpace. Reading it acknowledges
 * legacy INTx interrupts. PendingStatus carries the acknowledged bits to the
 * module event loop.
 */
typedef struct VirtioBlkInterruptResource {
    uint32_t         IsrOffset;
    atomic_uint_fast8_t PendingStatus;
} VirtioBlkInterruptResource_t;

typedef struct VirtioBlkDevice {
    element_t                  Header;
    BusDevice_t*               BusDevice;
    VirtioPciTransport_t       Transport;
    VirtioSplitQueue_t*        RequestQueue;
    StorageDescriptor_t        Descriptor;
    VirtioBlkInterruptResource_t InterruptResource;
    uuid_t                     InterruptId;
    int                        EventDescriptor;
    uint64_t                   Features;
    uint32_t                   SizeMax;
    uint32_t                   SegmentMax;
    int                        Registered;
} VirtioBlkDevice_t;

/**
 * @brief Acknowledges an INTx interrupt and wakes the module event loop.
 */
irqstatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t* interruptTable,
    _In_ InterruptResourceTable_t* resourceTable);

/**
 * @brief Creates and starts one modern PCI virtio block device.
 * @param busDevice Device supplied by the device manager. This function always
 *                  consumes ownership, including when initialization fails.
 * @param storageDeviceId Driver-local identifier exposed to the storage service.
 * @return An initialized device, or NULL when setup fails.
 */
VirtioBlkDevice_t*
VirtioBlkDeviceCreate(
    _In_ BusDevice_t* busDevice,
    _In_ uuid_t       storageDeviceId);

/**
 * @brief Stops a block device and releases all requests and transport resources.
 */
void
VirtioBlkDeviceDestroy(
    _In_ VirtioBlkDevice_t* device);

/**
 * @brief Queues a storage transfer and defers its protocol response.
 *
 * The supplied shared-memory handle remains attached until the device returns
 * the descriptor chain. A successful return therefore means that the response
 * will be sent later by VirtioBlkDeviceHandleInterrupt().
 */
oserr_t
VirtioBlkDeviceTransfer(
    _In_ VirtioBlkDevice_t*       device,
    _In_ struct gracht_message*   message,
    _In_ int                      direction,
    _In_ uint64_t                 sector,
    _In_ uuid_t                   bufferId,
    _In_ size_t                   bufferOffset,
    _In_ size_t                   sectorCount);

/**
 * @brief Drains completed request chains after a queue interrupt.
 */
oserr_t
VirtioBlkDeviceHandleInterrupt(
    _In_ VirtioBlkDevice_t* device);

/**
 * @brief Refreshes device configuration or recovers a device that requests reset.
 */
oserr_t
VirtioBlkDeviceHandleConfigurationChange(
    _InOut_ VirtioBlkDevice_t* device);

#endif /* __VIRTIO_BLOCK_H__ */
