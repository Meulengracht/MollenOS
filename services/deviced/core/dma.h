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
 * Device descriptions and the service code that handles their requests.
 */

#ifndef __DEVICED_CORE_DMA_H__
#define __DEVICED_CORE_DMA_H__

#include <device-dma.h>

// Forward declarations
struct DmDmaLease;

/**
 * @brief Prepare a device's DMA description and keep its registry entry alive.
 *
 * This is an internal service operation for trusted device owners, not a driver
 * protocol or authorization check. The existing request guard retains the entry
 * while its provider runs outside the registry lock. Success changes that guard
 * into a lease before dropping the lock. Removal winning the race discards the
 * description and returns busy. No buffers, hardware activation or driver grants
 * are created, and no pending flags are cleared.
 *
 * @param deviceId Exact registered device whose retained provider should run.
 * @param leaseOut Owning slot, initialized to NULL. Receives a lease on success;
 *                 remains NULL on failure. An occupied slot returns OS_EBUSY
 *                 unchanged. The caller must exclude reset while preparing and
 *                 using the description; a reference alone does not stop reset.
 * @return OS_EOK, OS_ENOENT for an absent device, OS_ENOTSUPPORTED for no DMA
 *         callback, OS_EBUSY for removal/occupied output, allocation/count errors,
 *         or the provider's preparation error.
 */
__EXTERN oserr_t
DmDevicePrepareDma(
    _In_    uuid_t              deviceId,
    _InOut_ struct DmDmaLease** leaseOut);

/**
 * @brief Borrow a prepared description while its owning lease remains alive.
 *
 * @param lease Owned lease, or NULL.
 * @return Read-only description, or NULL for no lease. Do not use the pointer
 *         after release. Copying these numbers does not extend their lifetime
 *         or make them usable after hardware reset.
 */
__EXTERN const struct DmDmaDescription*
DmDmaLeaseGetDescription(
    _In_ const struct DmDmaLease* lease);

/**
 * @brief Release the removal guard and clear the caller's owning slot.
 *
 * This remains available after removal starts, allowing the owner to release
 * the lease and retry removal. It frees only the prepared description and guard;
 * it is not a hardware stop or DMA-buffer release operation.
 *
 * @param lease Owning slot; NULL or an empty slot is harmless. Calls on the same
 *              slot must be serialized. Do not release copied aliases of the
 *              pointer: the slot represents one owner, not a reference count.
 */
__EXTERN void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease);

#endif
