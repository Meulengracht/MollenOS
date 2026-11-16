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
 * DMA routes, driver bindings and the leases that keep a bus alive.
 */

#ifndef __DEVICED_CORE_DMA_H__
#define __DEVICED_CORE_DMA_H__

#include <os/types/device.h>
#include <os/types/dma_attachment.h>
#include <dma-types.h>

// Forward declarations
struct DmDmaLease;

/**
 * @brief Set up the DMA manager and its event subscription. Call before device discovery.
 */
__EXTERN oserr_t
DmDmaInitialize(void);

/**
 * @brief Register a device's fixed DMA route with the kernel, once. If the error
 * is temporary, the saved route is kept so registration can be retried.
 */
__EXTERN oserr_t
DmDmaRegisterDevice(
    _In_ uuid_t device);

/**
 * @brief Remove the kernel's DMA state for a device once its removal has started.
 * On error the record is kept, so the removal can be tried again.
 */
__EXTERN oserr_t
DmDmaRemoveDevice(
    _In_ uuid_t device);

/**
 * @brief Stop all DMA below a registered host, using the bus's own stop callback.
 * This takes a temporary request on the host, so it also works while the host is
 * being removed. If the bus has no stop callback, this returns OS_ENOTSUPPORTED.
 * No bus in the tree can do a full stop yet.
 */
__EXTERN oserr_t
DmDeviceStopDma(
    _In_ uuid_t host);

/**
 * @brief Bind a registered device to its assigned driver. Each attempt carries a
 * request ID, so the same request can be repeated safely.
 * The sender is identified by its full principal from the transport. The kernel
 * checks the same live request again. A request that was retired, or that is
 * older than the saved one, cannot be reopened.
 * @param principal Sender identity supplied by the kernel, never read from the request.
 * @param device Registered device that is assigned to this sender.
 * @param requestId Nonzero and increasing with each new request. Must be below UINT64_MAX.
 * @param bindingOut Receives the driver's grant and generation. Only set on success.
 * @return OS_EOK, OS_EBUSY if an earlier request is still live, OS_ECANCELLED if
 * the request was retired or the route is blocked, or an authentication,
 * preparation or kernel error.
 */
__EXTERN oserr_t
DmDmaBind(
    _In_  uint64_t              principal,
    _In_  uuid_t                device,
    _In_  uint64_t              requestId,
    _Out_ OSDmaBindingResult_t* bindingOut);

/**
 * @brief Reads a device's DMA settings without preparing hardware or taking a lease.
 *
 * Holds a temporary request on the device, so it cannot be freed while the bus
 * DescribeDma callback runs without the registry lock. If device removal
 * starts during the callback, the result is thrown away and this returns OS_EBUSY.
 *
 * @param deviceId Registered device ID whose DescribeDma callback should run.
 * @param descriptionOut Filled in only on success (with DeviceId set by the
 *                       device manager). No lease is kept, so this data does
 *                       not keep the bus alive and may go stale if the device
 *                       is reset. Callers that need the route to stay the same
 *                       during publication must watch for host changes.
 * @return OS_EOK on success, OS_EINVALPARAMS if descriptionOut is NULL, OS_ENOENT
 *         if the device is missing, OS_EBUSY if removal started, OS_EOVERFLOW if
 *         too many requests are active, OS_ENOTSUPPORTED if no DMA route exists,
 *         or the bus callback's error.
 */
__EXTERN oserr_t
DmDeviceDescribeDma(
    _In_  uuid_t              deviceId,
    _Out_ DmDmaDescription_t* descriptionOut);

/**
 * @brief Prepares DMA for a device and hands back a lease that keeps it alive.
 *
 * A lease is a hold on the device entry that stops it from being removed while
 * a driver is using DMA. This takes a temporary request on the device so the
 * entry stays valid while PrepareDma runs without the registry lock, then
 * turns that request into a lease once PrepareDma succeeds. If device removal
 * starts during the call, ReleaseDma undoes the bus preparation and
 * *leaseOut stays NULL.
 *
 * Production callers hold the DMA lifecycle mutex from preparation until the
 * route has been checked, so assignment and recovery cannot change in between.
 *
 * @param deviceId Registered device ID that implements PrepareDma.
 * @param principal Calling process ID verified by the IPC server (never read
 *                  from the request payload).
 * @param leaseOut Address of a NULL lease pointer. Left unchanged on failure.
 *                 A lease only stops deviced from removing the device entry.
 *                 The caller must still stop the device from being reset while
 *                 it prepares and uses the description, and must stop DMA
 *                 before releasing the lease.
 * @return OS_EOK on success, OS_EPERMISSIONS for an unassigned principal,
 *         OS_EOVERFLOW if the principal cannot fit the existing bus interface,
 *         OS_EINVALPARAMS if leaseOut is NULL, OS_EBUSY if *leaseOut is already
 *         set or the device is being removed, or a lookup, memory, count
 *         overflow, or bus error.
 */
__EXTERN oserr_t
DmDevicePrepareDmaForPrincipal(
    _In_    uuid_t              deviceId,
    _In_    uint64_t            principal,
    _InOut_ struct DmDmaLease** leaseOut);

/**
 * @brief Returns the DMA description stored inside an active lease.
 *
 * @param lease Active lease pointer, or NULL.
 * @return Read-only description pointer, or NULL if lease is NULL. Do not
 *         use this pointer after DmDmaLeaseRelease, and do not keep copies of
 *         its addresses across a hardware reset.
 */
__EXTERN const struct DmDmaDescription*
DmDmaLeaseGetDescription(
    _In_ const struct DmDmaLease* lease);

/**
 * @brief Releases a DMA lease and clears the caller's pointer to NULL.
 *
 * Still works after device removal has started, so retiring a binding can
 * drop its lease and let a retried removal finish. Calls the bus ReleaseDma
 * callback (if set) before lowering the device's lease count and freeing
 * the lease. Callers must stop any running DMA and free DMA buffers first.
 *
 * @param lease Address of the caller's lease pointer. Passing NULL or a
 *              pointer to NULL does nothing. Do not release the same slot from
 *              two threads at once. Never copy the pointer to release it from
 *              two places, because a slot holds a single lease and is not
 *              reference counted.
 */
__EXTERN void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease);

/**
 * @brief Start shared hardware for a host without racing against recovery.
 * DmPublicationPrepareDma must have finished for the host's devices. On success
 * the lifecycle mutex stays held until DmDmaEndHostStart is called on the same
 * thread. Take bus locks only inside this scope. Do not call other DMA
 * lifecycle functions from inside it.
 */
__EXTERN oserr_t
DmDmaBeginHostStart(
    _In_ uuid_t host);

/** @brief Finish a successful DmDmaBeginHostStart. Release your bus locks first. */
__EXTERN void
DmDmaEndHostStart(void);

#endif //!__DEVICED_CORE_DMA_H__
