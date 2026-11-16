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
 * DMA configuration queries, active device leases, and per-process sessions.
 */

#ifndef __DEVICED_CORE_DMA_H__
#define __DEVICED_CORE_DMA_H__

#include <os/types/device.h>
#include <dma-types.h>

/**
 * @brief Initializes the DMA session list and lock.
 * Call this once at startup before any devices are published.
 */
__EXTERN void
DmDmaSessionsInitialize(void);

/**
 * @brief Handles a driver's DMA query or release request.
 *
 * When a process sends OSIOCTLREQUEST_DMA_QUERY, deviced creates a session
 * tied to that process, takes a DMA lease on the device so it stays registered
 * and powered, and hands back a session token. Querying again from the same
 * process returns that existing session. Sending OSIOCTLREQUEST_DMA_RELEASE with
 * the matching token tears down the session and drops the lease. If no session
 * exists or the token does not match, nothing changes and the call returns OS_EOK.
 *
 * @param principal Calling process ID verified by the IPC server.
 * @param device Registered device ID.
 * @param request Either OSIOCTLREQUEST_DMA_QUERY or OSIOCTLREQUEST_DMA_RELEASE.
 * @param buffer Holds OSDmaConfiguration on query, or the uint64_t session
 *               token on release.
 * @param length Size of buffer in bytes.
 * @return OS_EOK on success, OS_EBUSY if another process already holds a DMA
 *         session for this device, or a validation, memory, or bus error.
 */
__EXTERN oserr_t
DmDmaControl(
    _In_    uuid_t              principal,
    _In_    uuid_t              device,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length);

// Forward declarations
struct DmDmaLease;

/**
 * @brief Reads a device's DMA settings without preparing hardware or taking a lease.
 *
 * Holds a temporary request reference so the device cannot be freed while the
 * bus DescribeDma callback runs without the registry lock. If device removal
 * starts during the callback, the result is thrown away and this returns OS_EBUSY.
 *
 * @param deviceId Registered device ID whose DescribeDma callback should run.
 * @param descriptionOut Filled in only on success (with DeviceId set by the
 *                       device manager). Because no lease is kept, this data
 *                       does not keep the bus alive or remain valid if the
 *                       device is reset. Callers that rely on the route staying
 *                       fixed during publication must guard against host changes.
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
 * A lease is an active hold on the device entry that stops it from being
 * removed while a driver is running DMA. This takes a temporary request
 * reference so the entry stays safe while PrepareDma runs without the registry
 * lock, then swaps that request for a lease count once PrepareDma succeeds. If
 * device removal starts mid-call, ReleaseDma undoes any bus reference and
 * *leaseOut stays NULL.
 *
 * @param deviceId Registered device ID that implements PrepareDma.
 * @param principal Calling process ID verified by the IPC server (never read
 *                  from the request payload).
 * @param leaseOut Address of a NULL lease pointer. Left unchanged on failure.
 *                 A lease only stops deviced from removing the device entry, so
 *                 the caller must still prevent hardware resets while preparing
 *                 and using the description and stop DMA before releasing it.
 * @return OS_EOK on success, OS_EPERMISSIONS if principal is UUID_INVALID,
 *         OS_EINVALPARAMS if leaseOut is NULL, OS_EBUSY if *leaseOut is already
 *         set or the device is being removed, or a lookup, memory, count
 *         overflow, or bus error.
 */
__EXTERN oserr_t
DmDevicePrepareDmaForPrincipal(
    _In_    uuid_t              deviceId,
    _In_    uuid_t              principal,
    _InOut_ struct DmDmaLease** leaseOut);

/**
 * @brief Returns the DMA description stored inside an active lease.
 *
 * @param lease Active lease pointer, or NULL.
 * @return Read-only description pointer, or NULL if lease is NULL. Do not
 *         dereference this pointer after DmDmaLeaseRelease, and do not cache
 *         its addresses across a hardware reset.
 */
__EXTERN const struct DmDmaDescription*
DmDmaLeaseGetDescription(
    _In_ const struct DmDmaLease* lease);

/**
 * @brief Releases a DMA lease and clears the caller's pointer to NULL.
 *
 * Still works after device removal has started, so a driver or session can
 * drop its lease and let a retried removal finish. Calls the bus ReleaseDma
 * callback (if set) before decrementing the device's lease count and freeing
 * the lease. Callers must stop any active DMA and free DMA buffers first.
 *
 * @param lease Address of the caller's lease pointer. Passing NULL or a
 *              pointer to NULL does nothing. Serialize calls on the same slot,
 *              and never copy the pointer to release it from two places, because
 *              a slot tracks a single lease rather than a reference count.
 */
__EXTERN void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease);

#endif
