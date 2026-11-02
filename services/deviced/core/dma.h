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
 * DMA session ownership and device leases.
 */

#ifndef __DEVICED_CORE_DMA_H__
#define __DEVICED_CORE_DMA_H__

#include <os/types/device.h>
#include <dma-types.h>

/**
 * @brief Initialize DMA session bookkeeping before publishing devices.
 */
__EXTERN void
DmDmaSessionsInitialize(void);

/**
 * @brief Prepare or release a DMA session and its device lease.
 *
 * Call after DmDmaSessionsInitialize with a DMA query or release request.
 * Repeated queries from the same process return the existing session. Release
 * requires the matching session token. A missing session or mismatched token
 * leaves the current state unchanged and returns OS_EOK.
 *
 * @param principal Process identity used to match session ownership.
 * @param device Registered device identity.
 * @param request DMA_QUERY or DMA_RELEASE.
 * @param buffer Configuration output or exact session token to release.
 * @param length Size of the request buffer.
 * @return OS_EOK on success, OS_EBUSY when another process owns the session,
 *         or a validation, allocation, configuration or preparation error.
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
 * @brief Prepare a device's DMA description and keep its registry entry alive.
 *
 * The caller must be a trusted device owner. Driver requests use
 * DmDevicePrepareDmaForPrincipal to pass the authenticated process identity.
 * A request count keeps the entry alive while PrepareDma runs outside the
 * registry lock. Success replaces that count with a lease under the lock.
 * If removal starts during preparation, ReleaseDma undoes preparation when
 * available and the call returns OS_EBUSY.
 *
 * This registry operation allocates no DMA buffers and clears no pending flags.
 * Native DMA activation stays active until the paired ReleaseDma callback runs.
 *
 * @param deviceId Exact registered device whose PrepareDma callback should run.
 * @param leaseOut Owning slot, initialized to NULL. Receives a lease on success and
 *                 remains NULL on failure. An occupied slot returns OS_EBUSY
 *                 unchanged. The caller must exclude reset while preparing and
 *                 using the description; a reference alone does not stop reset.
 * @return OS_EOK, OS_ENOENT for an absent device, OS_ENOTSUPPORTED for no DMA
 *         callback, OS_EBUSY for removal/occupied output, allocation/count errors,
 *         or the PrepareDma callback's error. A NULL leaseOut returns OS_EINVALPARAMS.
 */
__EXTERN oserr_t
DmDevicePrepareDma(
    _In_    uuid_t              deviceId,
    _InOut_ struct DmDmaLease** leaseOut);

/**
 * @brief Prepare a DMA lease for a driver authenticated by the transport.
 *
 * The lifetime and failure cleanup rules match DmDevicePrepareDma. The caller
 * identity is passed to PrepareDma so the native DMA code can check activation
 * ownership before returning a description.
 *
 * @param deviceId Exact registered device.
 * @param principal Identity supplied by the transport, never a request field.
 * @param leaseOut Empty owning slot, unchanged on failure.
 * @return Preparation status or OS_EPERMISSIONS when principal is UUID_INVALID.
 */
oserr_t
DmDevicePrepareDmaForPrincipal(
    _In_    uuid_t              deviceId,
    _In_    uuid_t              principal,
    _InOut_ struct DmDmaLease** leaseOut);

/**
 * @brief Read a prepared description while its owning lease remains alive.
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
 * the lease and retry removal. ReleaseDma runs, when available, before the lease
 * count is decreased and the prepared description is freed. The caller must
 * stop DMA and release DMA buffers separately.
 *
 * @param lease Owning slot; NULL or an empty slot is harmless. Calls on the same
 *              slot must be serialized. Do not release copied aliases of the
 *              pointer: the slot represents one owner, not a reference count.
 */
__EXTERN void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease);

#endif
