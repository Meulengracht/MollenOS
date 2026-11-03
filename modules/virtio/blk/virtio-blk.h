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
 * negotiated yet.
 *
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#ifndef __VIRTIO_BLOCK_H__
#define __VIRTIO_BLOCK_H__

#include <ddk/devices/bus.h>
#include <ddk/interrupts/interrupt.h>
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
/** Maximum requested size of the single split request queue. */
#define VIRTIO_BLK_QUEUE_SIZE  128

/**
 * @brief Device-readable header at the start of every block request chain.
 *
 * Fields are little-endian. Sector is measured in VIRTIO_BLK_SECTOR_SIZE units,
 * regardless of the negotiated logical block size. The last descriptor points
 * to a status byte written by the device.
 */
PACKED_TYPESTRUCT(VirtioBlkRequestHeader, {
    uint32_t Type;      /**< VIRTIO_BLK_T_* operation requested by the driver. */
    uint32_t Reserved;  /**< Reserved by Virtio. The driver must write zero. */
    uint64_t Sector;    /**< First 512-byte sector accessed by the request. */
});

/**
 * @brief State shared with the fast interrupt handler.
 *
 * The ISR capability offset is relative to IoSpace. Reading it acknowledges
 * legacy INTx interrupts. PendingStatus saves the interrupt causes for the module
 * event loop. MSI-X reports both causes because they share one vector.
 */
typedef struct VirtioBlkInterruptResource {
    uint32_t            IsrOffset;
    // MSI-X shares vector zero between queues and configuration changes.
    int                 MsixEnabled;
    atomic_uint_fast8_t PendingStatus;
} VirtioBlkInterruptResource_t;

struct VirtioBlkRequest;

/**
 * @brief Block device configuration, request queue, and interrupt state.
 *
 * CleanupRequests contains requests whose resources could not be released.
 */
typedef struct VirtioBlkDevice {
    element_t                    Header;
    BusDevice_t*                 BusDevice;
    VirtioPciTransport_t         Transport;
    VirtioSplitQueue_t*          RequestQueue;
    StorageDescriptor_t          Descriptor;
    VirtioBlkInterruptResource_t InterruptResource;
    DeviceInterrupt_t            Interrupt;
    int                          EventDescriptor;
    uint64_t                     Features;
    uint32_t                     SizeMax;
    uint32_t                     SegmentMax;
    int                          Registered;
    struct VirtioBlkRequest*     CleanupRequests;
} VirtioBlkDevice_t;

/**
 * @brief Add a device to the cleanup list after initialization or teardown fails.
 *
 * The device must be removed from public lookup before calling this function.
 *
 * @param device Device whose DMA, interrupt or transport cleanup needs a retry.
 */
void
VirtioBlkDeferDestroy(
    _In_ VirtioBlkDevice_t* device);

/**
 * @brief Save interrupt status and wake the module event loop.
 * Reads the ISR byte to acknowledge INTx interrupts.
 */
irqstatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t* interruptTable,
    _In_ InterruptResourceTable_t* resourceTable);

/**
 * @brief Create and start one modern PCI virtio block device.
 *
 * @param busDevice Device supplied by the device manager. The driver frees this
 *                  descriptor during cleanup, including when initialization fails.
 *                  The caller must not free it after this call.
 * @param storageDeviceId Driver-local identifier exposed to the storage service.
 * @return An initialized device, or NULL when setup fails.
 */
VirtioBlkDevice_t*
VirtioBlkDeviceCreate(
    _In_ BusDevice_t* busDevice,
    _In_ uuid_t       storageDeviceId);

/**
 * @brief Register an initialized block device with the file service.
 *
 * The device must be in the driver's lookup collection before registration.
 * The file service may query its storage descriptor as soon as it is registered.
 */
oserr_t
VirtioBlkDeviceRegisterStorage(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Stop a block device and release all requests and transport resources.
 *
 * The caller must serialize device processing and remove the device from public
 * lookup first. If cleanup fails, the device and remaining resources are kept so
 * cleanup can be retried.
 *
 * @param device Device to destroy. NULL is accepted.
 * @return OS_EOK after release, otherwise a reset or cleanup error.
 */
oserr_t
VirtioBlkDeviceDestroy(
    _In_ VirtioBlkDevice_t* device);

/**
 * @brief Queue a storage transfer and send its response after completion.
 *
 * The shared-memory handle must stay attached while the device uses the request
 * buffers. On success, request completion sends the response after DMA and
 * resource cleanup. The caller must not send another response.
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
 * @brief Process completed request chains after a queue interrupt.
 */
oserr_t
VirtioBlkDeviceHandleInterrupt(
    _In_ VirtioBlkDevice_t* device);

/**
 * @brief Read device configuration or recover a device that requests a reset.
 */
oserr_t
VirtioBlkDeviceHandleConfigurationChange(
    _InOut_ VirtioBlkDevice_t* device);

#endif /* __VIRTIO_BLOCK_H__ */
