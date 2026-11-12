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
#include <os/fence.h>
#include <os/types/dma.h>
#include <os/types/handle.h>
#include <os/types/time.h>

_CODE_BEGIN

struct DMAContext;
struct DMABuffer;
struct DMAMapping;

/**
 * @brief One active use of a persistent mapping, stored in a driver job. Must be
 * zeroed before first use. Only DMABeginUse and DMAEndUse may change Mapping.
 * Do not copy an active token. Its memory must stay valid until DMAEndUse succeeds.
 */
struct DMAUse {
    // Mapping is the persistent mapping this use belongs to. NULL when the
    // token is empty.
    struct DMAMapping* Mapping;
};

/**
 * @brief One persistent DMA use and its completion fence, stored in a driver job.
 *
 * Must be zeroed before first use. Only DMABeginUseFenced and DMAEndUseFenced may
 * change it. Calls on the token and mapping must be serialized. Do not copy an
 * active token or pass its inner Use to the unfenced begin/end functions. Its
 * memory must stay valid until DMAEndUseFenced succeeds.
 *
 * Begin sets both fields on success. End clears Use.Mapping after DMAEndUse
 * succeeds, then signals the fence. Fence stays set until waiter notification
 * succeeds. If only Fence is set, notification must be retried before reuse.
 * The token is empty when both Use.Mapping and Fence are NULL.
 */
struct DMAFencedUse {
    // One counted use of the persistent mapping.
    struct DMAUse Use;
    // Fence reference held until the result is set and waiters are notified.
    OSFence_t*    Fence;
};

/**
 * @brief Requests a DMA session for a registered device (OSIOCTLREQUEST_DMA_QUERY)
 * and opens its DMA domain. Hardware is not enabled by this call. All drivers use
 * the same API, also for coherent devices. The kernel decides cache work and bounce
 * buffers from the DMA configuration of the session (cache policy and address
 * ranges). Device addresses are never assumed to be equal to CPU physical addresses.
 * Device access is still checked against the device identity, until Handle Grants
 * are used for this. The context is local to this process and cannot be sent over IPC.
 *
 * @param device Identity of the registered controller.
 * @param addressLimit Highest device address the controller can use (inclusive).
 * UINT64_MAX adds no limit.
 * @param contextOut *contextOut must be NULL on entry. If releasing the DMA session
 * fails while handling an error, a cleanup-only context may still be stored here.
 * Always close a non-NULL context. A cleanup-only context refuses allocations and
 * mappings, and can only be passed to DMAClose.
 * @return OS_EOK, OS_EBUSY if a context is already open, or an error from the DMA
 * session or the kernel.
 */
DDKDECL(oserr_t,
DMAOpen(
    _In_    uuid_t              device,
    _In_    uint64_t            addressLimit,
    _InOut_ struct DMAContext** contextOut));

/**
 * @brief Closes a context. All buffers and mappings must be released first.
 * Close must be serialized against create calls and other close calls. Separate
 * buffers and mappings can still be used concurrently. If close returns OS_EBUSY,
 * the context stays open and usable.
 * Once the kernel close has started, new buffers and mappings are refused. A failed
 * teardown is retried by calling DMAClose again with the same context. The retry
 * also releases the DMA session (OSIOCTLREQUEST_DMA_RELEASE) if that failed after
 * the kernel close succeeded. It also cleans up resources left over from a failed
 * create (these were never given to the driver).
 *
 * @param context Context slot, cleared on success. NULL or empty slots are a no-op.
 * @return OS_EOK, OS_EBUSY while buffers or mappings are still alive, or an error
 * from cleanup, the DMA session or the kernel.
 */
DDKDECL(oserr_t,
DMAClose(
    _InOut_ struct DMAContext** context));

/**
 * @brief Allocates zeroed memory that is shared by the CPU and the controller for
 * its whole lifetime. It is never bounced and needs no begin/end transfer calls.
 * The buffer holds both its CPU attachment and its device mapping. The driver must
 * still use barriers when giving descriptors to hardware and when reading completions.
 * A single segment is returned inline by the allocation syscall. Longer segment
 * lists are fetched once before this returns. The accessors never allocate, make
 * syscalls or fail while the buffer is alive.
 *
 * @param context Open device context. Must be serialized against DMAClose.
 * @param requirements Device address constraints. Size is rounded up to whole pages.
 * @param bufferOut *bufferOut must be NULL on entry. Unchanged on failure, also if
 * fetching the segment list fails.
 * @return OS_EOK, or an argument, allocation, translation or kernel error.
 */
DDKDECL(oserr_t,
DMAAllocate(
    _In_    struct DMAContext*            context,
    _In_    const struct DMARequirements* requirements,
    _InOut_ struct DMABuffer**            bufferOut));

/**
 * @brief Returns the CPU pointer of an allocation. Makes no kernel calls.
 * @param buffer Live allocation. Cleanup must not have started.
 * @return CPU pointer, valid until cleanup begins. Do not free it.
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
 * @brief Returns the device addresses for the entire allocation. The list never
 * changes. Makes no allocation or syscall.
 * @param buffer Live allocation. Cleanup must not have started.
 * @param countOut Receives the number of entries.
 * @return Segment list, valid until DMAFree succeeds. Do not free it.
 */
DDKDECL(const struct DMASegment*,
DMABufferSegments(
    _In_  const struct DMABuffer* buffer,
    _Out_ uint32_t*               countOut));

/**
 * @brief Releases an allocation. The driver must stop all hardware use of it first.
 * The device mapping is released before the CPU attachment. On failure the buffer
 * is kept so the call can be retried, also after a partial cleanup. Do not use it
 * for DMA again. This does not stop hardware or synchronize interrupt handlers.
 * @param buffer Buffer slot, cleared on success. NULL or empty slots are a no-op.
 * @return OS_EOK or a kernel cleanup error.
 */
DDKDECL(oserr_t,
DMAFree(
    _InOut_ struct DMABuffer** buffer));

/**
 * @brief Maps committed bytes of an SHM buffer for streaming DMA. DMABeginTransfer
 * must be called before hardware accesses the mapping. The kernel keeps its own
 * reference to the storage, separate from the caller's SHM attachment. The kernel
 * may bounce storage that the device cannot reach, that is misaligned, or that
 * shares cache lines in an unsafe way.
 * The segments describe the mapped bytes in buffer order, which can differ from the
 * physical layout. The driver must still handle the descriptor limits of its hardware.
 * The segment list is ready when this call succeeds.
 *
 * Requirements:
 * - They apply after address translation and after segments have been joined.
 * - Invalid alignments, or a length that is not a multiple of LengthAlignment,
 *   return OS_EINVALPARAMS. The mapped length is never padded.
 * - If bounce segments cannot meet valid requirements, the call fails with
 *   OS_ENOTSUPPORTED.
 * - Subranges and hardware descriptors must keep the requested alignment.
 *
 * @param context Open controller context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte relative to the whole SHM buffer (not its CPU view).
 * @param length Number of committed bytes to map. Must be nonzero. This call does not
 * commit missing pages.
 * @param direction Direction of hardware access, fixed for this mapping.
 * @param requirements Optional device segment constraints. NULL adds no constraints.
 * @param mappingOut *mappingOut must be NULL on entry. Unchanged on failure.
 * @return OS_EOK, or an argument, allocation, translation or kernel error.
 */
DDKDECL(oserr_t,
DMAMapSHM(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut));

/**
 * @brief Maps committed bytes of an SHM buffer using only their original pages.
 *
 * Works like DMAMapSHM, including the begin/end calls for cache synchronization.
 * Device addresses can differ from physical addresses, and the result can have
 * multiple segments. No bounce memory is allocated and no payload data is copied.
 * A reserved device pool is not needed.
 *
 * @param context Open controller context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte relative to the whole SHM buffer.
 * @param length Number of committed bytes to map. Must be nonzero.
 * @param direction Direction of hardware access, fixed for this mapping.
 * @param requirements Optional device segment constraints. NULL adds no constraints.
 * @param mappingOut *mappingOut must be NULL on entry. Unchanged on failure.
 * @return Same errors as DMAMapSHM, plus:
 *         - OS_ENOENT if the device cannot address the original pages.
 *         - OS_ENOTSUPPORTED if noncoherent device writes would share cache lines
 *           with bytes outside the mapped range, or if the original segments
 *           cannot meet the requirements.
 */
DDKDECL(oserr_t,
DMAMapSHMDirect(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut));

/**
 * @brief Maps the original committed pages of an SHM buffer for repeated, queued
 * device uses.
 *
 * The segment list does not change until DMAUnmap succeeds. The mapping keeps the
 * storage valid, separate from the caller's SHM attachment. No bounce copy or cache
 * work is done when the mapping is created.
 * Uses are counted per mapping. There is no read/write tracking per job. Completing
 * the fence of one job does not make the mapping idle.
 * How conflicts on shared ranges are handled is described by SHMWaitMode.
 *
 * Preconditions:
 * - The address and alignment requirements of DMAMapSHMDirect must be met.
 * - Calls on the mapping and its use tokens must be serialized.
 * - Device jobs that conflict within the mapping must be ordered by the driver.
 * - The CPU must stop accessing the bytes before DMABeginUse, and must not access
 *   them again until the last DMAEndUse succeeds.
 * - CPU access must go through an SHMAccessSession. This also applies to
 *   processes without a DMA context.
 * - Descriptor barriers and ordered register accesses are done by the driver.
 *   The driver must also detect completion and schedule dependent jobs.
 *
 * Errors:
 * - Same errors as DMAMapSHMDirect. Failure leaves mappingOut unchanged.
 *
 * @param context Open device context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte relative to the whole SHM buffer.
 * @param length Number of committed bytes to map. Must be nonzero.
 * @param direction Fixed device access direction for every use.
 * @param requirements Optional device segment constraints. NULL adds no constraints.
 * @param mappingOut *mappingOut must be NULL on entry. Unchanged on failure.
 * @return OS_EOK or an error described above.
 */
DDKDECL(oserr_t,
DMAMapSHMPersistent(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut));

/**
 * @brief Registers a use of the mapping. Must be called before the job can be seen
 * by hardware.
 *
 * The first use prepares the cache for the entire mapping. Later uses only update
 * the use count. If a conflicting request is queued, the current batch of uses may
 * have to finish first. Makes one syscall, with no allocation or segment lookup.
 * Wait behavior is described by SHMWaitMode.
 *
 * Preconditions:
 * - The mapping must be a live persistent mapping, and the use token must be zeroed.
 * - Calls on the mapping and its tokens must be serialized.
 * - CPU access must stop before the first begin, and may only start again after
 *   all uses have ended.
 *
 * Errors:
 * - OS_EBUSY for an active token, or when the current batch must finish first
 *   because a request is queued.
 * - OS_EOVERFLOW if the use count cannot be increased.
 * - OS_EINVALPARAMS for invalid arguments or a mapping that is not persistent.
 * - Errors from the shared wait and other kernel errors are passed through.
 * Failure leaves the use token unchanged.
 *
 * @param mapping Live persistent mapping. All calls on it must be serialized.
 * @param use Zeroed token stored in the job. Unchanged on failure.
 * @param mode How to handle conflicts, see SHMWaitMode.
 * @param deadline Absolute timeout, or NULL to wait forever. Ignored for SHMWaitNonBlocking.
 * @return OS_EOK or an error described above.
 */
DDKDECL(oserr_t,
DMABeginUse(
    _In_    struct DMAMapping*   mapping,
    _InOut_ struct DMAUse*       use,
    _In_    enum SHMWaitMode     mode,
    _In_    const OSTimestamp_t* deadline));

/**
 * @brief Ends a use when the job has completed, when hardware is confirmed stopped,
 * or when submitting the job failed.
 *
 * After a failed submission, end may only be called if hardware could not have
 * started the job. The last end synchronizes the entire mapping for CPU access.
 * Earlier ends do no cache work and do not allow CPU access, not even to completed
 * bytes. Uses can end in any order. Makes one syscall and no allocation.
 *
 * @param use Active token. Do not copy it or end it twice. On success it is empty
 *            and can be used again. On failure it stays active so the call can be
 *            retried.
 * @return OS_EOK, or an argument or kernel error. Does not wait for or stop hardware.
 */
DDKDECL(oserr_t,
DMAEndUse(
    _InOut_ struct DMAUse* use));

/**
 * @brief Begins a persistent DMA use and makes its completion fence pending.
 *
 * Must be called before hardware can see the job. Adds a fence reference and
 * reserves the idle fence before calling DMABeginUse. The fence becomes pending
 * only after begin succeeds. Makes no allocation. Waiting and cache work follow
 * DMABeginUse and SHMWaitMode.
 *
 * The token keeps its fence reference until completion notification succeeds.
 * After begin succeeds, the caller may release its original reference. Give
 * consumers separate references first and notify them after begin succeeds,
 * before they poll or wait. The driver must detect completion and call
 * DMAEndUseFenced.
 *
 * Preconditions:
 * - The mapping and token requirements of DMABeginUse must be met.
 * - The fence must be idle, and the caller must hold a reference on it.
 * - Only this token may prepare, cancel, submit or signal the fence while active.
 *   Do not call those functions separately.
 * - For shared fences, keep the local SHM mapping valid until DMAEndUseFenced and
 *   all other local users finish. A fence reference does not keep that mapping alive.
 *
 * Errors:
 * - Same errors as DMABeginUse.
 * - OS_EBUSY for a fence that is not idle, or a token that is active.
 * - OS_EOVERFLOW if the local fence reference count cannot be increased.
 * Invalid inputs are left unchanged. If kernel begin fails, the fence returns to
 * idle and the token stays empty so the call can be retried.
 *
 * @param mapping Persistent mapping. Calls on it must be serialized.
 * @param use Zeroed job token. Must stay valid until end succeeds, also on retry.
 * @param fence Idle fence, signaled by DMAEndUseFenced after this use ends.
 * @param mode How to handle conflicts, see SHMWaitMode.
 * @param deadline Absolute timeout, or NULL to wait forever. Ignored for SHMWaitNonBlocking.
 * @return OS_EOK or an error described above.
 */
DDKDECL(oserr_t,
DMABeginUseFenced(
    _In_    struct DMAMapping*   mapping,
    _InOut_ struct DMAFencedUse* use,
    _In_    OSFence_t*           fence,
    _In_    enum SHMWaitMode     mode,
    _In_    const OSTimestamp_t* deadline));

/**
 * @brief Ends a completed or stopped use, and signals the fence with its result.
 *
 * Hardware must have completed or be confirmed stopped for this job. After a
 * failed submission, end may only be called if hardware could not have started
 * the job. An error result does not stop the device. Calls on the token and
 * mapping must be serialized.
 *
 * Calls DMAEndUse before setting the result. Other uses of the mapping may still
 * be active. Only the last end synchronizes the cache, so a completed fence alone
 * does not allow CPU access. Use SHMAccessBeginCpu after conflicting DMA uses end.
 *
 * If DMAEndUse fails, the use stays active and the fence stays pending. If waiter
 * notification fails, the DMA use has ended and consumers can already read the
 * result. The token keeps its fence reference so notification can be retried
 * without ending the use twice. Retry with the same result.
 *
 * @param use Active token or token awaiting notification retry. Empty and reusable
 *            on success.
 * @param result Result of the job, such as OS_EOK, OS_ECANCELLED or OS_EDEVFAULT.
 *               Must match the previous result when retrying notification.
 * @return OS_EOK, a DMAEndUse or notification error, or OS_EINVALPARAMS for an empty
 *         token or a changed result on retry. Does not stop or wait for hardware.
 */
DDKDECL(oserr_t,
DMAEndUseFenced(
    _InOut_ struct DMAFencedUse* use,
    _In_    oserr_t              result));

/**
 * @brief Returns the device address list of the mapping. The list never changes,
 * and no kernel calls are made.
 * @param mapping Live mapping. Must be serialized against DMAUnmap on this mapping.
 * @param countOut Receives the number of entries.
 * @return Segment list, valid until DMAUnmap succeeds, also across begin and end
 *         calls. Do not free it.
 */
DDKDECL(const struct DMASegment*,
DMAMappingSegments(
    _In_  const struct DMAMapping* mapping,
    _Out_ uint32_t*                countOut));

/**
 * @brief Hands the whole mapping to the device. Must be called before the
 * descriptors are given to the hardware.
 * The given range decides the cache work, bounce copies and shared range checks.
 * CPU sessions that conflict stay blocked until DMAEndTransfer succeeds.
 * Does not start or wait for hardware.
 * Makes one syscall on every architecture, with no RPC or allocation.
 * Wait behavior is described by SHMWaitMode.
 *
 * Preconditions:
 * - The streaming mapping must be idle, and the range must be nonempty and inside it.
 * - Calls on the mapping must be serialized.
 * - CPU sessions that conflict must end before begin. CPU access can start again
 *   with SHMAccessBeginCpu after DMAEndTransfer succeeds.
 *
 * Errors:
 * - OS_EBUSY if the mapping is already active.
 * - Errors from the shared wait, argument errors and kernel errors are passed through.
 * Failure leaves access unchanged.
 *
 * @param mapping Idle streaming mapping.
 * @param offset First active byte relative to the mapped range.
 * @param length Number of active bytes. Must be nonzero.
 * @param mode How to handle conflicts, see SHMWaitMode.
 * @param deadline Absolute timeout, or NULL to wait forever. Ignored for SHMWaitNonBlocking.
 * @return OS_EOK or an error described above.
 */
DDKDECL(oserr_t,
DMABeginTransfer(
    _In_ struct DMAMapping*   mapping,
    _In_ size_t               offset,
    _In_ size_t               length,
    _In_ enum SHMWaitMode     mode,
    _In_ const OSTimestamp_t* deadline));

/**
 * @brief Takes the whole mapping back from the device after the transfer has
 * completed, or after hardware is confirmed stopped.
 * Only the completed part of the range given to begin is copied back, counted from
 * the start of that range. A count of zero ends a cancelled transfer without copying
 * any received bytes. Does not detect completion or stop hardware. Makes one syscall,
 * with no RPC or allocation.
 *
 * @param mapping Streaming mapping with an active device transfer.
 * @param completedLength Number of completed bytes from the start of the range. At
 *                        most the active length.
 * @return OS_EOK, or an argument, state or kernel error. On failure the transfer
 *         stays active with its current range, so the call can be retried.
 */
DDKDECL(oserr_t,
DMAEndTransfer(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             completedLength));

/**
 * @brief Releases an idle streaming or persistent mapping. On failure the mapping
 * is kept. Never stops hardware, copies data back or destroys the caller's SHM
 * attachment. Do not call this on copies of the mapping pointer. A successful unmap
 * invalidates the segment list. CPU sessions stay valid without the mapping.
 * @param mapping Mapping slot, cleared on success. NULL or empty slots are a no-op.
 * @return OS_EOK, OS_EBUSY while device access is active or queued, or a kernel error.
 */
DDKDECL(oserr_t,
DMAUnmap(
    _InOut_ struct DMAMapping** mapping));

_CODE_END
#endif
