/**
 * Copyright 2026, Philip Meulengracht
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
 * Internal device registry structures and request lifetime helpers shared
 * between devices.c and dma-device.c.
 */

#ifndef __DEVICED_DEVICE_PRIVATE_H__
#define __DEVICED_DEVICE_PRIVATE_H__

#include <device-bus.h>
#include <dma-types.h>
#include <ds/list.h>
#include <os/usched/mutex.h>

/**
 * @brief One registered device inside deviced, protected by g_dmDevicesLock.
 *
 * ActiveRequests counts in-flight operations (such as ioctls or DescribeDma)
 * that run without g_dmDevicesLock held, so the entry and its Bus.Context stay
 * valid until those calls finish. A successful PrepareDma swaps one
 * ActiveRequests count for one DmaLeases count. Once Removing is set, new
 * requests and leases are turned away while existing ones finish and drain.
 */
typedef struct DmDevice {
    element_t                 Header;
    uuid_t                    DriverId;
    uint64_t                  DriverPrincipal;
    bool                      HasDriver; // Set while a driver lookup is running or has matched.
    unsigned int              Flags;
    Device_t*                 Device;
    list_t                    Protocols; // list<struct __DmDeviceProtocol>
    DmDeviceDescriptionKind_t Kind;
    DmDeviceBus_t             Bus;
    unsigned int              ActiveRequests;
    unsigned int              DmaLeases;
    int                       Removing;
} DmDevice_t;

/**
 * @brief Holds an active DMA lease on a device and a copy of its DMA settings.
 * As long as this lease exists, Device->DmaLeases stays nonzero and the device
 * cannot be destroyed. Leases have no timeout and stay active until released.
 */
struct DmDmaLease {
    DmDevice_t*             Device;
    struct DmDmaDescription Description;
};

// Protects the device list in devices.c and per-device fields above. Drop this
// lock before invoking Bus.Operations callbacks, since bus code may take its
// own locks or trigger device removal.
extern struct usched_mtx g_dmDevicesLock;

/**
 * @brief Looks up a registered device by ID while g_dmDevicesLock is held.
 * Does not increment any reference count. Keep g_dmDevicesLock held, or acquire
 * a request or DMA lease before unlocking, so the entry cannot be freed.
 *
 * @param deviceId Registered device ID.
 * @return Device entry, or NULL if no matching device is registered.
 */
__EXTERN DmDevice_t*
DmDeviceFindLocked(
    _In_ uuid_t deviceId);

/**
 * @brief Increments ActiveRequests on a device so it stays valid outside the lock.
 * Takes and releases g_dmDevicesLock internally, and leaves deviceOut unchanged
 * if the call fails. Fails with OS_EBUSY once device removal has started.
 *
 * @param deviceId Registered device ID.
 * @param deviceOut Receives the device pointer, valid until DmDeviceRequestRelease.
 * @return OS_EOK on success, OS_ENOENT if missing, OS_EBUSY if removing, or
 *         OS_EOVERFLOW if ActiveRequests would wrap.
 */
__EXTERN oserr_t
DmDeviceRequestAcquire(
    _In_  uuid_t       deviceId,
    _Out_ DmDevice_t** deviceOut);

/**
 * @brief Decrements ActiveRequests on a device acquired via DmDeviceRequestAcquire.
 * Takes and releases g_dmDevicesLock internally. Once this returns, another
 * thread may destroy device unless another request or DMA lease is active.
 *
 * @param device Entry returned by DmDeviceRequestAcquire.
 */
__EXTERN void
DmDeviceRequestRelease(
    _In_ DmDevice_t* device);

#endif //!__DEVICED_DEVICE_PRIVATE_H__
