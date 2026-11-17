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
 * Kernel interface for device DMA region sessions. A shared-memory buffer
 * created with SHM_REGIONS can be split into fixed, cache-line-aligned regions.
 * Once a device maps those regions, a driver opens a session on one region
 * with pre-allocated token slots so starting and ending transfers never
 * allocates memory.
 */

#ifndef __SHM_DEVICE_REGION_H__
#define __SHM_DEVICE_REGION_H__

#include <shm/device.h>
#include <shm/record.h>
#include <os/types/dma_region.h>

DECL_STRUCT(SHMDeviceRegionSession);

/**
 * @brief Open a DMA session with fixed token slots on one region of a mapping.
 *
 * Pre-allocating slots here keeps later Begin and End calls free of heap
 * allocations. The caller must hold an active SHMDeviceOperationSubmit
 * operation on mapping so the mapping stays open while this session links.
 * Pass a fresh record in the SHMRecordUpdating state with Object set to NULL.
 * This sets record->Object before linking the session onto mapping, so if
 * linking or copying resultOut to user space fails, the caller cleans up and
 * removes record to free the session.
 *
 * @param mapping   Live SHMDeviceMappingRegions mapping covering the region.
 * @param identity  Buffer, region index, and generation identifying the target region.
 * @param capacity  Number of token slots to allocate (1 to SHM_REGION_TOKEN_LIMIT).
 * @param record    New SHMRecord in SHMRecordUpdating with Object == NULL.
 * @param resultOut Receives the region offset, length, identity, and capacity.
 * @return OS_EOK on success, OS_EINVALPARAMS for wrong mapping kind or bad
 *         capacity, OS_ENOENT if identity is not in mapping, OS_EBUSY if
 *         mapping is closing, or OS_EOOM if allocation fails.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionSessionCreate(
    _In_  SHMDeviceMapping_t*        mapping,
    _In_  const OSBufferIdentity_t* identity,
    _In_  unsigned int              capacity,
    _In_  SHMRecord_t*               record,
    _Out_ OSDmaRegionSession_t*     resultOut);

/**
 * @brief Close a region session through its handle record.
 *
 * Normal close (abandon == false) succeeds only when every token slot is idle
 * or finished and the mapping is not cleaning up. record->Object stays set
 * until the session is unlinked from the mapping, then clears before freeing
 * the session. Keeping it set until unlinked lets a retried close finish if
 * an earlier close was interrupted.
 *
 * Process exit cleanup (abandon == true) may only run after all threads in the
 * caller's process group have stopped. If a token is still running on hardware,
 * it revokes the device binding, detaches record->Object, and marks the session
 * abandoned so mapping recovery frees it once the hardware stops.
 *
 * @param record  Handle record wrapping the region session.
 * @param abandon False for normal user close, true for final process teardown.
 * @return OS_EOK on success (including when abandoned for later cleanup), or
 *         OS_EBUSY when abandon is false and tokens or mapping cleanup are busy.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionSessionDestroy(
    _In_ SHMRecord_t* record,
    _In_ bool         abandon);

/**
 * @brief Start a DMA transfer in one token slot of a region session.
 *
 * Reserves the region against conflicting CPU or device accesses, runs any
 * needed CPU cache maintenance for direction, and marks the slot active. Each
 * use of a slot must pass the next nonzero generation number (1, 2, 3, ...) so
 * a late End call from an older cycle cannot touch a newer transfer. If this
 * fails, the slot keeps its prior finished generation and result untouched.
 *
 * @param record     Handle record for session.
 * @param session    Open region session from record->Object.
 * @param slot       Token slot index (0 to capacity - 1).
 * @param generation Next expected nonzero cycle number for this slot.
 * @param direction  Transfer direction allowed by the parent mapping.
 * @return OS_EOK on success, OS_EBUSY if the slot or region conflicts,
 *         OS_EDEVFAULT if the region previously faulted, OS_ECANCELLED if the
 *         mapping or binding stopped, OS_EPERMISSIONS if direction is wider
 *         than the mapping allows, or OS_EINVALPARAMS for bad arguments.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionBegin(
    _In_ SHMRecord_t*              record,
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ unsigned int              generation,
    _In_ enum DMADirection         direction);

/**
 * @brief Finish a DMA transfer in one token slot after hardware completes.
 *
 * Invalidates CPU caches if the device wrote to memory and releases the region
 * reservation. Repeating this call with the same slot and generation returns
 * the saved result without repeating cache work.
 *
 * @param record     Handle record for session.
 * @param session    Open region session from record->Object.
 * @param slot       Token slot index passed to SHMDeviceRegionBegin.
 * @param generation Exact generation number passed to SHMDeviceRegionBegin.
 * @return OS_EOK on normal completion, OS_EDEVFAULT if the device stopped
 *         during the transfer, OS_EBUSY if recovery is still draining it,
 *         OS_ENOENT if unknown or mismatched generation, or OS_EINVALPARAMS.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionEnd(
    _In_ SHMRecord_t*              record,
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ unsigned int              generation);

/**
 * @brief Finish or undo an interrupted Begin or End call during process exit.
 *
 * Only call this after every thread in the caller's process security group has
 * stopped. Revokes the device binding first so active hardware accesses fault,
 * then undoes an interrupted Begin or finishes an interrupted End.
 *
 * @param record Handle record whose PendingOperation was left mid-syscall.
 * @return OS_EOK on success or when nothing is pending, or a failure status
 *         keeping the pending operation attached so cleanup can retry.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionCancel(
    _In_ SHMRecord_t* record);

/**
 * @brief Verify that a generic SHM handle record holds a DMA device mapping.
 *
 * System calls that accept a mapping handle use this before casting or reading
 * record->Object.
 *
 * @param record Record returned by SHMRecordBegin. May be NULL.
 * @return True when record wraps a SHMDeviceMapping, false otherwise.
 */
KERNELABI bool KERNELAPI
SHMDeviceIsMappingRecord(
    _In_ const SHMRecord_t* record);

#endif //!__SHM_DEVICE_REGION_H__
