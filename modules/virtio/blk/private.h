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
 * These helpers operate on a device owned by the module lifecycle. Callers must
 * serialize lifecycle, configuration, and request processing for that device;
 * the common virtqueue lock does not protect block-driver state. None of these
 * helpers may run in the fast interrupt handler.
 *
 * Request objects and their DMA attachments remain private to request.c.
 * device.c orchestrates recovery, while request.c alone cancels and releases
 * the outstanding request contexts after hardware ownership has ended.
 */

#ifndef __VIRTIO_BLK_PRIVATE_H__
#define __VIRTIO_BLK_PRIVATE_H__

#include "virtio-blk.h"

/**
 * @brief Read capacity, logical sector size, and negotiated SG limits.
 *
 * Requires an initialized transport with device->Features already negotiated.
 * Retries when ConfigGeneration changes during the read. Returns OS_EOK for a
 * stable, supported geometry, OS_EBUSY if retries are exhausted, or a read/
 * geometry error. Fields are updated while reading; failure does not roll back
 * the previous configuration, so the caller must not treat it as a new snapshot.
 */
oserr_t
VirtioBlkReadConfiguration(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Reset the device, cancel outstanding requests, and destroy its queue.
 *
 * Requires a live transport; RequestQueue may be NULL. Successful cancellation
 * sends each deferred response once and releases its DMA attachments. On
 * success RequestQueue is NULL. If reset fails, requests and queue memory stay
 * allocated because the device may still access them. A later abort/destroy
 * failure likewise leaves the remaining queue state with the caller.
 */
oserr_t
VirtioBlkResetRequestQueue(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Cancel pending work and restart the device with a new request queue.
 *
 * Requires an initialized device and its existing interrupt registration.
 * Resets requests, renegotiates features, refreshes configuration, creates the
 * queue, and sets DRIVER_OK. On failure marks the device failed and unregisters
 * storage. A failed reset retains DMA ownership as described above.
 *
 * May complete and free any queued request, including the request whose failed
 * notification triggered recovery; callers must not access it afterwards.
 */
oserr_t
VirtioBlkRecoverDevice(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Create the module event and register the fast interrupt handler.
 *
 * Requires mapped ISR capability resources, InterruptId == UUID_INVALID, and
 * EventDescriptor == -1. Call before DRIVER_OK so initial completions can wake
 * the module. On failure releases any event created here; on success the device
 * owns the event and interrupt registration until unregister.
 */
oserr_t
VirtioBlkRegisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device);

/**
 * @brief Unregister the fast handler and remove/close its module event.
 *
 * Accepts a partially initialized device. Call once during teardown, before
 * releasing the ISR mapping or device memory. Does not reset the queue or
 * cancel requests; hardware DMA ownership is handled separately.
 */
void
VirtioBlkUnregisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device);

#endif //!__VIRTIO_BLK_PRIVATE_H__
