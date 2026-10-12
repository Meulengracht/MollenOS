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
 */

#ifndef __DDK_DMA_H__
#define __DDK_DMA_H__

#include <ddk/ddkdefs.h>
#include <os/types/dma.h>
#include <os/types/handle.h>

_CODE_BEGIN

// Forward declarations
struct DMAMapping;

/**
 * @brief Open DMA access for an assigned device, with its inclusive address limit.
 * The grant must come from authorized device registration; a deviced DeviceId
 * or a physical address is not a grant. Drivers cannot supply ranges or cache
 * policy. UINT32_MAX limits device addresses to 32 bits; UINT64_MAX adds no limit.
 *
 * This release returns OS_ENOTSUPPORTED: device grants and recovery are not yet
 * connected. There is deliberately no fallback to physical SHM addresses.
 *
 * @param deviceGrant Kernel-issued device grant belonging to this caller.
 * @param addressLimit Largest address the controller can use, including the last byte.
 * @param contextOut Owning slot initialized to NULL; unchanged on failure.
 * @return OS_EOK, OS_EINVALPARAMS, OS_EBUSY for an occupied slot, OS_EOOM,
 *         OS_ENOTSUPPORTED while unavailable, or a kernel error.
 */
DDKDECL(oserr_t,
DMAOpen(
    _In_  uuid_t   deviceGrant,
    _In_  uint64_t addressLimit,
    _Out_ uuid_t*  handleOut));

/**
 * @brief Allocate zeroed SHM for structures the CPU and controller keep sharing.
 * The allocation never bounces. Requirements apply to device addresses and may
 * fail even when ordinary RAM is available. Segments cover the page-rounded
 * allocation. Use SHMBuffer(bufferOut) for CPU access and DMAGetSegments for
 * hardware addresses. Keep ring publication/completion barriers in the driver.
 *
 * Stop hardware before cleanup. First DMAUnmap the mapping; only after success
 * destroy the SHM handle with OSHandleDestroy. These are separate owners. Do not
 * pass this mapping to the streaming sync calls; both sides keep using it.
 *
 * @param context Open context; no hardware is started by allocation.
 * @param requirements Byte length, alignment, boundary and contiguous requirement.
 * @param bufferOut Empty OSHandle (zero initialized); unchanged on failure.
 * @param mappingOut Owning slot initialized to NULL; unchanged on failure.
 * @return OS_EOK, argument/allocation errors, or the kernel error. No physical
 *         allocation or weaker requirement is substituted on failure.
 */
DDKDECL(oserr_t,
DMAAllocate(
    _In_    uuid_t                              dmaHandle,
    _In_    const struct SHMDeviceRequirements* requirements,
    _InOut_ OSHandle_t*                         bufferOut,
    _InOut_ struct DMAMapping**                 mappingOut));

/**
 * @brief Map committed, SHM-owned bytes for a transfer and retain their storage.
 * The kernel may use the original pages or a bounce buffer. The result starts
 * CPU-owned. An arbitrary exported pointer or physical scatter/gather list is
 * not an acceptable substitute. The kernel must check caller rights to SHM.
 *
 * @param context Open context for the controller that will perform the transfer.
 * @param buffer Existing SHM handle. The mapping retains storage independently.
 * @param offset First byte relative to the entire SHM buffer, not its CPU view.
 * @param length Nonzero bytes to map; does not implicitly commit missing pages.
 * @param direction ToDevice means hardware reads; FromDevice means it writes.
 * @param mappingOut Owning slot initialized to NULL; unchanged on failure.
 * @return OS_EOK, argument/allocation errors, or the kernel error.
 */
DDKDECL(oserr_t,
DMAMapSHM(
    _In_    uuid_t                  dmaHandle,
    _In_    const OSHandle_t*       buffer,
    _In_    size_t                  offset,
    _In_    size_t                  length,
    _In_    enum SHMDeviceDirection direction,
    _InOut_ struct DMAMapping**     mappingOut));

/**
 * @brief Borrow the device-address list, copied into local storage on first use.
 * Entries describe the mapped bytes in buffer order. They can differ from SHM's
 * physical entries because translation can split ranges or use bounce pages.
 * No entry implies a controller-specific maximum transfer length: drivers must
 * still split transfer descriptors as their hardware requires.
 *
 * @param mapping Live mapping; serialize calls with sync and release on this object.
 * @param segmentsOut Borrowed read-only list, valid until successful DMAUnmap.
 * @param countOut Number of entries; both outputs remain unchanged on failure.
 * @return OS_EOK, OS_EOOM, OS_EINVALPARAMS, or a kernel error. A query failure
 *         leaves the mapping owned and available for retry or unmap.
 */
DDKDECL(oserr_t,
DMAGetSegments(
    _In_  struct DMAMapping*              mapping,
    _Out_ const struct SHMDeviceSegment** segmentsOut,
    _Out_ uint32_t*                       countOut));

/**
 * @brief Publish transfer data before submitting it to hardware.
 * Cache handling and bounce copies happen in the kernel. Ownership is for the
 * whole mapping: the CPU and other SHM users must not touch any of its bytes
 * until completion and DMASyncForCpu. Calls on one mapping must be serialized.
 *
 * @param mapping Streaming mapping from DMAMapSHM; not a DMAAllocate result.
 * @param offset Offset relative to the mapped range, not the whole SHM buffer.
 * @param length Nonzero bytes the device will use.
 * @return OS_EOK, OS_EBUSY if already device-owned, or an argument/kernel error.
 */
DDKDECL(oserr_t,
DMASyncForDevice(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             offset,
    _In_ size_t             length));

/**
 * @brief Reclaim transfer data after completion or confirmed hardware stop.
 * This does not wait for or stop hardware. For a bounced receive, only completed
 * bytes are copied back. The whole mapping returns to CPU ownership, even when
 * fewer bytes completed. It is not safe to reclaim while another transfer uses it.
 *
 * @param mapping Streaming mapping that was handed to the device.
 * @param offset First completed byte relative to the mapped range.
 * @param length Completed bytes; zero allows reclaim after a cancelled transfer.
 * @return OS_EOK, or an argument/ownership/kernel error.
 */
DDKDECL(oserr_t,
DMASyncForCpu(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             offset,
    _In_ size_t             length));

/**
 * @brief Release a device mapping without losing a busy or failed resource.
 * On success, clears the owning slot and invalidates borrowed segment pointers.
 * On failure, keeps everything for retry. Does not destroy a separately returned
 * SHM handle, stop hardware, or implicitly copy back a transfer. NULL/empty slots
 * are harmless. Never release copied aliases or serialize these local pointers.
 * @return OS_EOK, OS_EBUSY while a streaming mapping is device-owned, or a kernel error.
 */
DDKDECL(oserr_t,
DMAUnmap(
    _InOut_ struct DMAMapping** mapping));

/**
 * @brief Close a context; existing mappings retain their own kernel references.
 * Does not stop hardware or release mappings. Clears the owning slot only on
 * success. NULL/empty slots are harmless. Serialize calls on the same object;
 * do not share its pointer between processes or release copied aliases.
 * @return OS_EOK, or a kernel error with the owning slot unchanged.
 */
DDKDECL(oserr_t,
DMAClose(
    _In_ uuid_t dmaHandle));

_CODE_END

#endif //!__DDK_DMA_H__
