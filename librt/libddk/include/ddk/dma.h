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

struct DMAContext;
struct DMABuffer;
struct DMAMapping;

/**
 * @brief Retain a registered device's provider path and open its DMA domain.
 * Does not enable hardware. All drivers use the same API, including coherent
 * devices. The kernel selects cache work and bounce backing from the provider
 * policy; device addresses are never assumed to equal CPU physical addresses.
 * Device authorization still uses the existing device identity pending Handle
 * Grants. The context is local to this process and cannot be copied over IPC.
 *
 * @param device Registered controller identity.
 * @param addressLimit Inclusive maximum device address; UINT64_MAX adds no limit.
 * @param contextOut Empty owning slot. On failure it may retain a cleanup-only
 * context if provider rollback failed; always close a non-NULL slot. Such a
 * context refuses allocations and mappings and is usable only with DMAClose.
 * @return OS_EOK, OS_EBUSY for an existing context, or provider/kernel error.
 */
DDKDECL(oserr_t,
DMAOpen(
    _In_    uuid_t              device,
    _In_    uint64_t            addressLimit,
    _InOut_ struct DMAContext** contextOut));

/**
 * @brief Close a context after all buffers and mappings have been released.
 * Serialize close against creation and other close calls. Separate children may
 * be used concurrently. A busy close leaves an open context usable. Once kernel
 * close starts, creation is refused; failed teardown is retried with this same
 * object, including provider release after successful kernel close. Also retries
 * any unpublished resources retained after a failed creation's cleanup.
 *
 * @param context Owning slot; cleared on success. NULL/empty slots are harmless.
 * @return OS_EOK, OS_EBUSY for live children, or cleanup/provider/kernel error.
 */
DDKDECL(oserr_t,
DMAClose(
    _InOut_ struct DMAContext** context));

/**
 * @brief Allocate zeroed memory continuously shared by the CPU and controller.
 * Never bounces and needs no streaming handoffs. The object owns both its CPU
 * attachment and device mapping. Driver publication/completion barriers are
 * still required. A single segment arrives inline in the allocation syscall;
 * fragmented lists are fetched once before success. Accessors never allocate,
 * perform syscalls or fail for a live object.
 *
 * @param context Open device context; serialize creation against context close.
 * @param requirements Device-address constraints; size is rounded to whole pages.
 * @param bufferOut Empty owning slot; unchanged on failure, including list failure.
 * @return OS_EOK or argument, allocation, translation or kernel error.
 */
DDKDECL(oserr_t,
DMAAllocate(
    _In_    struct DMAContext*            context,
    _In_    const struct DMARequirements* requirements,
    _InOut_ struct DMABuffer**            bufferOut));

/**
 * @brief Borrow the CPU view of a live allocation. Performs no kernel calls.
 * @param buffer Live allocation, not undergoing cleanup.
 * @return CPU pointer, valid until cleanup begins; never free it directly.
 */
DDKDECL(void*,
DMABufferData(
    _In_ const struct DMABuffer* buffer));

/**
 * @brief Get the actual, page-rounded allocation size without a kernel call.
 * @param buffer Live allocation.
 * @return Bytes covered by both the CPU view and device segments.
 */
DDKDECL(size_t,
DMABufferSize(
    _In_ const struct DMABuffer* buffer));

/**
 * @brief Borrow immutable device addresses for the entire allocation.
 * @param buffer Live allocation, not undergoing cleanup.
 * @param countOut Receives the number of entries.
 * @return Borrowed list valid until successful free; no allocation or syscall.
 */
DDKDECL(const struct DMASegment*,
DMABufferSegments(
    _In_  const struct DMABuffer* buffer,
    _Out_ uint32_t*               countOut));

/**
 * @brief Release an allocation after the driver has stopped hardware use.
 * Releases the device mapping before its CPU attachment. Failure retains the
 * owner for retry, including partial cleanup; do not use it again for DMA.
 * This does not halt hardware or synchronize interrupt handlers.
 * @param buffer Owning slot; cleared on success. NULL/empty slots are harmless.
 * @return OS_EOK or kernel cleanup error.
 */
DDKDECL(oserr_t,
DMAFree(
    _InOut_ struct DMABuffer** buffer));

/**
 * @brief Map committed SHM-owned bytes for streaming DMA, initially CPU-owned.
 * Retains the storage independently of the caller's SHM attachment. The kernel
 * may bounce inaccessible or cache-line-unsafe storage. Arbitrary exported
 * pointers are unsupported. Segments describe the mapped bytes in buffer order,
 * not the original physical layout; hardware descriptor limits remain the
 * driver's responsibility. The segment list is ready when this call succeeds.
 *
 * @param context Open controller context; serialize creation against close.
 * @param buffer Existing SHM handle; its attachment remains caller-owned.
 * @param offset First byte relative to the whole SHM buffer, not its CPU view.
 * @param length Nonzero committed bytes to map; missing pages are not committed.
 * @param direction Direction of hardware access, fixed for this mapping.
 * @param mappingOut Empty owning slot; unchanged on failure.
 * @return OS_EOK or argument, allocation, translation or kernel error.
 */
DDKDECL(oserr_t,
DMAMapSHM(
    _In_    struct DMAContext*  context,
    _In_    const OSHandle_t*   buffer,
    _In_    size_t              offset,
    _In_    size_t              length,
    _In_    enum DMADirection   direction,
    _InOut_ struct DMAMapping** mappingOut));

/**
 * @brief Borrow the mapping's immutable device-address list without kernel work.
 * @param mapping Live mapping; serialize calls with release on this object.
 * @param countOut Receives the number of entries.
 * @return Borrowed list valid until successful unmap, including across handoffs.
 */
DDKDECL(const struct DMASegment*,
DMAMappingSegments(
    _In_  const struct DMAMapping* mapping,
    _Out_ uint32_t*                countOut));

/**
 * @brief Hand the whole mapping to the device before publishing descriptors.
 * The active range controls cache work and bounce copies, not ownership scope.
 * No CPU or other SHM user may access any mapped bytes until successful end.
 * Calls on a mapping must be serialized. Does not start or wait for hardware.
 * Uses one syscall on every architecture, with no RPC or allocation.
 *
 * @param mapping CPU-owned streaming mapping.
 * @param offset First active byte relative to the mapped range.
 * @param length Nonzero active byte count.
 * @return OS_EOK, OS_EBUSY if already active, or argument/kernel error. Failure
 *         leaves CPU ownership unchanged.
 */
DDKDECL(oserr_t,
DMABeginTransfer(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             offset,
    _In_ size_t             length));

/**
 * @brief Reclaim the whole mapping after completion or confirmed hardware stop.
 * Copies back only the completed prefix of the range supplied to begin. A zero
 * count reclaims a cancelled transfer without copying any receive bytes. Does
 * not detect completion or stop hardware. Uses one syscall, no RPC/allocation.
 *
 * @param mapping Device-owned streaming mapping.
 * @param completedLength Completed prefix length, at most the active length.
 * @return OS_EOK or argument/ownership/kernel error. Failure preserves the active
 *         range and device ownership for retry.
 */
DDKDECL(oserr_t,
DMAEndTransfer(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             completedLength));

/**
 * @brief Release a CPU-owned streaming mapping, retaining its owner on failure.
 * Never stops hardware, copies back data or destroys the caller's SHM attachment.
 * Do not release copied aliases. Successful unmap invalidates borrowed segments.
 * @param mapping Owning slot; cleared on success. NULL/empty slots are harmless.
 * @return OS_EOK, OS_EBUSY while device-owned, or kernel error.
 */
DDKDECL(oserr_t,
DMAUnmap(
    _InOut_ struct DMAMapping** mapping));

_CODE_END
#endif
