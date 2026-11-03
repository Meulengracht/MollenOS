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
 
 * Internal interfaces between the virtio-blk implementation files.
 *
 * Callers must serialize device setup, teardown, configuration and request
 * processing. The common virtqueue lock does not protect block-driver state.
 * These helpers must not run in the fast interrupt handler.
 *
 * Request objects and their DMA attachments are private to request.c. Device
 * recovery runs in device.c, but request.c cancels and frees outstanding
 * requests once the hardware has stopped using their memory.
 */

#ifndef __VIRTIO_BLK_PRIVATE_H__
#define __VIRTIO_BLK_PRIVATE_H__

#include "virtio-blk.h"

/**
 * @brief Read capacity, logical sector size, and negotiated SG limits.
 *
 * Requires an initialized transport and negotiated device->Features. If
 * ConfigGeneration changes during the read, we retry. Fields are updated as
 * they are read, so the caller must not use the configuration after a failure.
 *
 * @param device Device whose configuration is being read.
 * @return OS_EOK for stable, supported geometry. OS_EBUSY if all retries fail,
 *         otherwise a read or geometry error.
 */
oserr_t
VirtioBlkReadConfiguration(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Reset the device, cancel outstanding requests, and destroy its queue.
 *
 * Requires a live transport. RequestQueue may be NULL. Cancellation releases
 * each request's DMA attachments before sending its deferred response once.
 * On success, RequestQueue is NULL. If reset fails, we keep the requests and
 * queue memory because the device may still access them. If cancellation or
 * queue destruction fails, the remaining state is kept for another attempt.
 *
 * @param device Device whose request queue is being stopped.
 * @return OS_EOK after reset and cleanup, otherwise a reset or cleanup error.
 */
oserr_t
VirtioBlkResetRequestQueue(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Cancel pending work and restart the device with a new request queue.
 *
 * Requires an initialized device with interrupts registered. Resets requests
 * and renegotiates features, then reads the configuration and creates a new
 * queue before setting DRIVER_OK. On failure, marks the device failed and
 * unregisters storage. If reset fails, the device may still use request memory.
 *
 * Any queued request may be completed and freed here, including the request
 * whose notification failed. Callers must not access that request afterwards.
 *
 * @param device Device to restart.
 * @return OS_EOK after restart, otherwise a reset or initialization error.
 */
oserr_t
VirtioBlkRecoverDevice(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Create the module event and register the fast interrupt handler.
 *
 * Requires mapped ISR resources, no allocated interrupt and EventDescriptor
 * set to -1. Call before DRIVER_OK so the first completions can wake the module.
 * On failure, tries to release the event and interrupt resources created here.
 * If cleanup fails, the remaining resources are kept for another unregister
 * attempt. On success, they remain until VirtioBlkUnregisterInterrupt is called.
 *
 * @param device Device receiving the interrupt registration.
 * @return OS_EOK on success, otherwise an event or interrupt setup error.
 */
oserr_t
VirtioBlkRegisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Unregister the fast handler and close its module event.
 *
 * Stop device interrupts before calling this. Release the ISR mapping and
 * device memory only after success. A partially initialized device and repeated
 * calls are accepted. On failure, the interrupt allocation and event are kept
 * so the caller can retry. Queue reset and request cancellation are separate.
 *
 * @param device Device whose interrupt resources are being released.
 * @return OS_EOK on success, otherwise an interrupt cleanup error.
 */
oserr_t
VirtioBlkUnregisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Retry DMA cleanup for completed requests.
 *
 * Requires serialized device processing. Deferred replies wait until the
 * device has stopped using the buffers and all request resources are released.
 *
 * @param device Device with requests waiting for cleanup.
 * @return OS_EOK when no requests need cleanup, otherwise a cleanup error.
 */
oserr_t
VirtioBlkRetryRequests(
    _InOut_ VirtioBlkDevice_t* device);

#endif //!__VIRTIO_BLK_PRIVATE_H__
