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
 * @brief One running use of a persistent mapping. The driver keeps it in its job
 * structure. It must be zeroed before first use. Only DMABeginUse and DMAEndUse may
 * change Mapping. Do not copy an active token. Its memory must stay valid until
 * DMAEndUse succeeds.
 */
struct DMAUse {
    // The persistent mapping this use belongs to. NULL when the token is empty.
    struct DMAMapping* Mapping;
};

/**
 * @brief One persistent DMA use together with the fence that reports its completion.
 * The driver keeps it in its job structure. A fence is an object that other code
 * can wait on until the job's result is set.
 *
 * Must be zeroed before first use. Only DMABeginUseFenced and DMAEndUseFenced may
 * change it. Calls on the token and the mapping must be serialized. Do not copy an
 * active token. Do not pass its inner Use to DMABeginUse or DMAEndUse. Its memory
 * must stay valid until DMAEndUseFenced succeeds.
 *
 * A successful begin sets both fields. The end call clears Use.Mapping once
 * DMAEndUse succeeds, and then signals the fence. Fence stays set until the waiters
 * have been notified. If only Fence is set, the notification failed and must be
 * retried before the token is reused. The token is empty when both Use.Mapping and
 * Fence are NULL.
 */
struct DMAFencedUse {
    // The use of the persistent mapping, counted by the mapping.
    struct DMAUse Use;
    // Fence reference held until the result is set and the waiters are notified.
    OSFence_t*    Fence;
};

/**
 * @brief Gets a binding for the assigned controller and opens a DMA context for it.
 * A binding is the permission, issued by the device service, to do DMA for one
 * device. The DMA context is what the driver passes to every allocation and mapping
 * call. It lives in this process and cannot be sent to another process.
 *
 * This call does not enable the hardware. The kernel works out the device address
 * ranges and cache policy from the attachment registered for the device.
 * addressLimit can only lower the limit. Device addresses may differ from CPU
 * physical addresses.
 *
 * @param device Identity of the registered controller assigned to this driver.
 * @param addressLimit Highest device address the controller can use (inclusive).
 * Use UINT64_MAX when the controller has no limit of its own.
 * @param contextOut Must point to a NULL pointer. If the reply from the device
 * service is lost or a cleanup step fails, a context that only exists for cleanup
 * can be returned together with an error. Always close a non-NULL context. Such a
 * context refuses allocation and mapping, and only DMAClose can be called on it.
 * @return OS_EOK, OS_EBUSY if the device already has a context in this process, or
 * the error from the device service or the kernel. OS_EPROTOCOL means the request
 * to the device service has an unknown outcome.
 */
DDKDECL(oserr_t,
DMAOpen(
    _In_    uuid_t              device,
    _In_    uint64_t            addressLimit,
    _InOut_ struct DMAContext** contextOut));

/**
 * @brief Releases a context. All buffers and mappings made from it must be released
 * first. Calls must be serialized with creation and with other close calls. If
 * live buffers or mappings remain, the call returns OS_EBUSY and the context stays
 * usable. Once teardown has started, creating buffers and mappings is refused.
 * If the close fails, call it again with the same slot. Steps that already
 * succeeded are not repeated.
 *
 * A normal close does not contact the device service. If the reply to the binding
 * request was lost during DMAOpen, close sends that request again to find out what
 * happened. If that cannot be resolved, for example because the device was revoked,
 * the cleanup context stays until the process exits. Closing never stops the
 * hardware, and a leftover cleanup context is not proof that it has stopped.
 *
 * @param context Slot that is cleared on success. NULL or an empty slot does
 * nothing.
 * @return OS_EOK, OS_EBUSY while buffers or mappings remain, or a cleanup or
 * transport error.
 */
DDKDECL(oserr_t,
DMAClose(
    _InOut_ struct DMAContext** context));

/**
 * @brief Allocates zeroed memory that the CPU and the controller share for the whole
 * life of the buffer. Use it for long-lived structures such as command rings and
 * descriptor tables. The device works on this memory directly, so no copy is made
 * and no begin or end transfer calls are needed.
 *
 * The buffer holds both the CPU view and the device mapping. The driver must still
 * use memory barriers when it gives descriptors to the hardware and when it reads
 * completions. A memory barrier stops the CPU from reordering memory accesses
 * around it.
 *
 * A single segment is returned by the allocation syscall itself. A longer segment
 * list is fetched before this call returns. So the accessors never allocate, make
 * syscalls or fail while the buffer is alive.
 *
 * @param context Open device context. Must be serialized against DMAClose.
 * @param requirements Alignment and layout the device needs. The size is rounded up
 * to whole pages.
 * @param bufferOut Must point to a NULL pointer. Unchanged on failure, including
 * when fetching the segment list fails.
 * @return OS_EOK, or an argument, allocation, address translation or kernel error.
 */
DDKDECL(oserr_t,
DMAAllocate(
    _In_    struct DMAContext*            context,
    _In_    const struct DMARequirements* requirements,
    _InOut_ struct DMABuffer**            bufferOut));

/**
 * @brief Returns the CPU pointer to an allocation. Makes no kernel calls.
 * @param buffer Live allocation. Cleanup must not have started.
 * @return CPU pointer, valid until cleanup begins. Do not free it.
 */
DDKDECL(void*,
DMABufferData(
    _In_ const struct DMABuffer* buffer));

/**
 * @brief Returns the real allocation size, rounded up to whole pages. Makes no
 * kernel call.
 * @param buffer Live allocation.
 * @return Number of bytes covered by both the CPU view and the device segments.
 */
DDKDECL(size_t,
DMABufferSize(
    _In_ const struct DMABuffer* buffer));

/**
 * @brief Returns the device addresses that cover the whole allocation. The list
 * never changes. Makes no allocation or syscall.
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
 * The device mapping is released before the CPU view. If this fails, the buffer is
 * kept so the call can be retried, even after a partial cleanup. Do not use the
 * buffer for DMA again. This call does not stop the hardware and does not wait for
 * interrupt handlers that may still be running.
 * @param buffer Buffer slot, cleared on success. NULL or an empty slot does nothing.
 * @return OS_EOK or a kernel cleanup error.
 */
DDKDECL(oserr_t,
DMAFree(
    _InOut_ struct DMABuffer** buffer));

/**
 * @brief Maps committed bytes of an SHM buffer for streaming DMA. An SHM buffer is
 * a block of shared memory, and committed bytes are pages that already exist.
 * A streaming mapping handles one transfer at a time. Call DMABeginTransfer before
 * the hardware touches the memory and DMAEndTransfer when it is done. The kernel
 * keeps its own reference to the storage, separate from the caller's SHM
 * attachment, so the pages stay alive while they are mapped.
 *
 * The kernel may use a bounce buffer instead of the original pages. A bounce buffer
 * is temporary memory that the device can reach. Data is copied into it before a
 * transfer and copied back after. This happens when the device cannot reach the
 * original pages, when they do not meet the alignment requirements, or when a
 * non-coherent device would write into a cache line that also holds bytes outside
 * the range. Dropping such a cache line could destroy the CPU's changes to those
 * neighbouring bytes.
 *
 * Behind an IOMMU the device can only be given whole IOMMU granules, often 4096
 * bytes. A range that is not whole granules, such as a 512-byte storage request,
 * is copied through a bounce buffer of whole granules. The buffer is zeroed before
 * the device can reach it, and the bytes after the request are private padding.
 * The segment list still has one segment of exactly the requested length, and
 * transfers can only name bytes inside it. The device can reach the padding, so a
 * device overrun lands there and never in other memory. Each such mapping holds at
 * least one granule of the reserved device pool for as long as it exists, and at
 * most 262144 bytes.
 *
 * The segments describe the mapped bytes in buffer order, which can differ from the
 * physical layout. The driver must still respect the descriptor limits of its
 * hardware. The segment list is ready when this call succeeds.
 *
 * Requirements:
 * - They apply to the final device segments, after address translation and after
 *   neighbouring segments have been joined.
 * - An alignment that is not a power of two, or a length that is not a multiple of
 *   LengthAlignment, returns OS_EINVALPARAMS. The mapped length is never padded.
 * - If even bounce segments cannot meet valid requirements, the call fails with
 *   OS_ENOTSUPPORTED.
 * - When the driver splits a segment or adds an offset, the pieces must still keep
 *   the requested alignment.
 *
 * @param context Open controller context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte to map, counted from the start of the whole SHM buffer
 * and not from the caller's CPU view.
 * @param length Number of committed bytes to map. Must be nonzero. This call does not
 * commit missing pages.
 * @param direction How the hardware will access the memory. Fixed for the life of
 * this mapping.
 * @param requirements Optional limits on the device segments. NULL means no limits.
 * @param mappingOut Must point to a NULL pointer. Unchanged on failure.
 * @return OS_EOK, or an argument, allocation, address translation or kernel error.
 *         Two results concern the bounce buffer behind an IOMMU:
 *         - OS_EBUFFER if the copy would need more than 262144 bytes of
 *           storage. The same length fails again. Map the range in smaller pieces.
 *         - The pool allocator's error, OS_EOOM today, if the device pool is
 *           short. Other mappings hold the storage, so the same request can work
 *           later.
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
 * Works like DMAMapSHM, including the begin and end calls that do the cache work.
 * Device addresses can differ from physical addresses, and the result can have
 * several segments. No bounce buffer is allocated and no data is copied, so no
 * reserved pool of device-reachable memory is needed.
 *
 * @param context Open controller context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte to map, counted from the start of the whole SHM buffer.
 * @param length Number of committed bytes to map. Must be nonzero.
 * @param direction How the hardware will access the memory. Fixed for the life of
 * this mapping.
 * @param requirements Optional limits on the device segments. NULL means no limits.
 * @param mappingOut Must point to a NULL pointer. Unchanged on failure.
 * @return Same errors as DMAMapSHM, plus:
 *         - OS_ENOENT if the device cannot reach the original pages.
 *         - OS_ENOTSUPPORTED if a non-coherent device would write into cache lines
 *           that also hold bytes outside the mapped range, or if the original
 *           segments cannot meet the requirements.
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
 * @brief Size limits for one piece (chunk) of a transfer mapped by DMAMapSHMChunk.
 *
 * All three values are in bytes. DirectLength and BounceLength must each be at
 * least BlockLength. A limit that is not a whole number of blocks is rounded
 * down to whole blocks before use. The limits are only read during the call, so
 * one structure can be shared by every request of a driver.
 */
struct DMAChunkLimits {
    // Largest length to map when the device can use the original pages. This
    // costs no pool memory, so it can be large, e.g. 1 MiB.
    size_t DirectLength;
    // Largest length to map when the pages may have to be copied through a
    // bounce buffer. Each byte of such a mapping holds device pool memory for as
    // long as the mapping exists, so keep this small, e.g. 128 KiB.
    size_t BounceLength;
    // The mapped length is always a whole number of blocks, e.g. the 512-byte
    // sector size of a storage device. Must be nonzero.
    size_t BlockLength;
};

/**
 * @brief Maps the next piece (chunk) of an SHM transfer for streaming DMA. The
 * chunk is kept small enough to work when the pages must be copied.
 *
 * Use it instead of DMAMapSHM when a driver moves a large client buffer one piece
 * at a time, as a storage driver does. A plain DMAMapSHM call on a large range
 * can fail or starve other drivers when the kernel copies the data:
 * - A bounce buffer is temporary memory that the device can reach. The kernel
 *   copies data through it when the device cannot use the original pages.
 * - Bounce buffers come from the device pool, a small reserved block of
 *   device-reachable memory (4 MiB on ARM64, 2 MiB on x86) that every driver and
 *   every DMAAllocate call shares.
 * - Without an IOMMU, one bounce mapping takes one contiguous piece of the pool.
 *   Behind an IOMMU, a bounce mapping above 262144 bytes fails with OS_EBUFFER.
 *
 * The call works in three steps:
 * 1. Map min(length, DirectLength) with DMAMapSHMDirect. This uses the original
 *    pages and no pool memory.
 * 2. If the device cannot use the original pages (OS_ENOENT or OS_ENOTSUPPORTED
 *    from step 1), map min(length, BounceLength) with DMAMapSHM. DMAMapSHM still
 *    uses the original pages when the smaller range allows it.
 * 3. If step 2 fails with OS_EOOM or OS_EBUFFER, halve the length, round it down
 *    to whole blocks and try again. The smallest attempt is one block.
 *
 * The result is an ordinary streaming mapping. Call DMABeginTransfer,
 * DMAEndTransfer and DMAUnmap on it exactly as for DMAMapSHM. The returned length
 * can be shorter than the bytes left in the transfer. To map the next chunk, add
 * the mapped length to offset, subtract it from length, and call again.
 *
 * Preconditions:
 * - The same rules as DMAMapSHM apply to buffer, offset, direction and
 *   requirements. Requirements are passed unchanged to both map calls.
 * - length must be a nonzero multiple of BlockLength.
 * - If requirements has a nonzero LengthAlignment, BlockLength should be a
 *   multiple of it. Otherwise a shrunk length can fail with OS_EINVALPARAMS.
 *
 * Errors:
 * - OS_EINVALPARAMS if a required pointer is NULL, *mappingOut is not NULL,
 *   BlockLength is zero, length is zero or not a multiple of BlockLength, or
 *   DirectLength or BounceLength is smaller than BlockLength. No map call is made.
 * - OS_EOOM or OS_EBUFFER if even a one-block bounce mapping failed. The device
 *   pool is short. Other mappings hold the memory, so retry the same call later,
 *   for example after one of the driver's own transfers has completed.
 * - Any other error from DMAMapSHMDirect or DMAMapSHM is returned unchanged,
 *   without further attempts.
 *
 * @param context Open controller context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte to map, counted from the start of the whole SHM buffer.
 * @param length Number of committed bytes left in the transfer, starting at offset.
 * @param direction How the hardware will access the memory. Fixed for the life of
 * this mapping.
 * @param requirements Optional limits on the device segments. NULL means no limits.
 * @param limits Chunk size limits. See struct DMAChunkLimits.
 * @param mappingOut Must point to a NULL pointer. Unchanged on failure.
 * @param mappedLengthOut Receives the number of mapped bytes on success. This is a
 * nonzero multiple of BlockLength and never larger than length. Unchanged on
 * failure.
 * @return OS_EOK or an error described above.
 */
DDKDECL(oserr_t,
DMAMapSHMChunk(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _In_    const struct DMAChunkLimits*     limits,
    _InOut_ struct DMAMapping**              mappingOut,
    _Out_   size_t*                          mappedLengthOut));

/**
 * @brief Maps the original committed pages of an SHM buffer for repeated, queued
 * device jobs. A queued job is one the driver has submitted to the hardware, which
 * can still be waiting or running while more jobs are submitted.
 *
 * The segment list does not change until DMAUnmap succeeds. The mapping keeps the
 * storage alive, separate from the caller's SHM attachment. No bounce copy or cache
 * work happens when the mapping is created.
 * Uses are counted per mapping. The kernel does not track reads and writes per job,
 * so one finished job does not make the mapping idle. SHMWaitMode describes how
 * conflicts on shared ranges are handled.
 *
 * Preconditions:
 * - The address and alignment requirements of DMAMapSHMDirect must be met.
 * - Calls on the mapping and its use tokens must be serialized.
 * - If jobs conflict within the mapping, for example one writes bytes that another
 *   reads, the driver must put them in a safe order.
 * - The CPU must stop accessing the bytes before DMABeginUse, and must not access
 *   them again until the last DMAEndUse succeeds.
 * - CPU access must go through an SHMAccessSession, which tells the kernel when the
 *   CPU is using a range. This also applies to processes without a DMA context.
 * - The driver handles descriptor barriers and the order of register writes. It
 *   must also detect completion and start any dependent jobs.
 *
 * Errors:
 * - Same errors as DMAMapSHMDirect. Failure leaves mappingOut unchanged.
 *
 * @param context Open device context. Must be serialized against DMAClose.
 * @param buffer Existing SHM handle. The caller keeps its own attachment.
 * @param offset First byte to map, counted from the start of the whole SHM buffer.
 * @param length Number of committed bytes to map. Must be nonzero.
 * @param direction How the hardware will access the memory. Fixed for every use.
 * @param requirements Optional limits on the device segments. NULL means no limits.
 * @param mappingOut Must point to a NULL pointer. Unchanged on failure.
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
 * @brief Registers a use of the mapping. Must be called before the hardware can see
 * the job.
 *
 * The first use prepares the CPU caches for the whole mapping. Later uses only
 * raise the use count. If a conflicting request is already waiting, the uses that
 * are running may have to finish before this one can start. Makes one syscall,
 * with no allocation or segment lookup. Waiting follows SHMWaitMode.
 *
 * Preconditions:
 * - The mapping must be a live persistent mapping, and the use token must be zeroed.
 * - Calls on the mapping and its tokens must be serialized.
 * - CPU access must stop before the first begin, and may only start again after
 *   all uses have ended.
 *
 * Errors:
 * - OS_EBUSY if the token is already active, or if a conflicting request is
 *   waiting and the running uses must finish first.
 * - OS_EOVERFLOW if the use count cannot be increased.
 * - OS_EINVALPARAMS for invalid arguments or a mapping that is not persistent.
 * - Errors from the wait (see SHMWaitMode) and other kernel errors are returned
 *   unchanged.
 * Failure leaves the use token unchanged.
 *
 * @param mapping Live persistent mapping. All calls on it must be serialized.
 * @param use Zeroed token stored in the job. Unchanged on failure.
 * @param mode How to handle conflicts. See SHMWaitMode.
 * @param deadline Absolute time to stop waiting, or NULL to wait forever. Ignored for
 * SHMWaitNonBlocking.
 * @return OS_EOK or an error described above.
 */
DDKDECL(oserr_t,
DMABeginUse(
    _In_    struct DMAMapping*   mapping,
    _InOut_ struct DMAUse*       use,
    _In_    enum SHMWaitMode     mode,
    _In_    const OSTimestamp_t* deadline));

/**
 * @brief Ends a use. Call it when the job has completed, when the hardware is
 * confirmed stopped, or when submitting the job failed.
 *
 * After a failed submission, only call it if the hardware could not have started
 * the job. The last end prepares the whole mapping for CPU access again. Earlier
 * ends do no cache work and do not allow CPU access, not even to bytes that are
 * done. Uses can end in any order. Makes one syscall and no allocation.
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
 * @brief Begins a persistent DMA use and sets its completion fence to pending.
 *
 * Must be called before the hardware can see the job. It takes a reference on the
 * fence and reserves the idle fence before it calls DMABeginUse. The fence becomes
 * pending only after begin succeeds. Makes no allocation. Waiting and cache work
 * are the same as for DMABeginUse and SHMWaitMode.
 *
 * The token holds its fence reference until the waiters have been notified. Once
 * begin succeeds, the caller may drop its own reference. Give each consumer its own
 * reference first, and hand the fence over after begin succeeds so that nobody polls
 * or waits on an idle fence. The driver must detect completion and call
 * DMAEndUseFenced.
 *
 * Preconditions:
 * - The mapping and token requirements of DMABeginUse must be met.
 * - The fence must be idle, and the caller must hold a reference on it.
 * - While the token is active, only the token may prepare, cancel, submit or signal
 *   the fence. Do not call those functions on it separately.
 * - For shared fences, keep the local SHM mapping valid until DMAEndUseFenced and
 *   all other local users are done. A fence reference does not keep that mapping
 *   alive.
 *
 * Errors:
 * - Same errors as DMABeginUse.
 * - OS_EBUSY if the fence is not idle or the token is active.
 * - OS_EOVERFLOW if the local fence reference count cannot be increased.
 * Invalid arguments leave everything unchanged. If the kernel begin fails, the fence
 * goes back to idle and the token stays empty, so the call can be retried.
 *
 * @param mapping Persistent mapping. Calls on it must be serialized.
 * @param use Zeroed job token. Must stay valid until end succeeds, also on retry.
 * @param fence Idle fence. DMAEndUseFenced signals it after this use ends.
 * @param mode How to handle conflicts. See SHMWaitMode.
 * @param deadline Absolute time to stop waiting, or NULL to wait forever. Ignored for
 * SHMWaitNonBlocking.
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
 * @brief Ends a completed or stopped use and signals the fence with the result.
 *
 * The hardware must have completed this job or be confirmed stopped. After a failed
 * submission, only call it if the hardware could not have started the job. An error
 * result does not stop the device. Calls on the token and mapping must be
 * serialized.
 *
 * DMAEndUse is called first and the result is set after it. Other uses of the
 * mapping may still be running. Only the last end prepares the caches for the CPU,
 * so a completed fence alone does not allow CPU access. Call SHMAccessBeginCpu once
 * all conflicting DMA uses have ended.
 *
 * If DMAEndUse fails, the use stays active and the fence stays pending. If
 * notifying the waiters fails, the DMA use has already ended and consumers can read
 * the result. The token keeps its fence reference so the notification can be
 * retried without ending the use twice. Retry with the same result.
 *
 * @param use Active token, or a token waiting for a notification retry. Empty and
 *            reusable on success.
 * @param result Result of the job, such as OS_EOK, OS_ECANCELLED or OS_EDEVFAULT.
 *               Must match the earlier result when retrying a notification.
 * @return OS_EOK, a DMAEndUse or notification error, or OS_EINVALPARAMS for an empty
 *         token or a changed result on retry. Does not stop or wait for hardware.
 */
DDKDECL(oserr_t,
DMAEndUseFenced(
    _InOut_ struct DMAFencedUse* use,
    _In_    oserr_t              result));

/**
 * @brief Returns the device addresses that cover the mapping. The list never
 * changes. Makes no kernel calls.
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
 * @brief Starts a transfer on a streaming mapping. Must be called before the
 * descriptors are given to the hardware.
 *
 * The whole mapping is marked busy, but only the given range gets cache work, bounce
 * copies and conflict checks against other users of the shared memory. CPU
 * sessions that conflict stay blocked until DMAEndTransfer succeeds. Does not start
 * the hardware or wait for it. Makes one syscall on every architecture, with no
 * request to a service and no allocation. Waiting follows SHMWaitMode.
 *
 * Preconditions:
 * - The streaming mapping must be idle, and the range must be nonempty and inside it.
 * - Calls on the mapping must be serialized.
 * - CPU sessions that conflict must end before begin. CPU access can start again
 *   with SHMAccessBeginCpu after DMAEndTransfer succeeds.
 *
 * Errors:
 * - OS_EBUSY if the mapping is already active.
 * - Errors from the wait (see SHMWaitMode), argument errors and kernel errors are
 *   returned unchanged.
 * Failure leaves access unchanged.
 *
 * @param mapping Idle streaming mapping.
 * @param offset First byte of the transfer, counted from the start of the mapped
 * range.
 * @param length Number of bytes in the transfer. Must be nonzero.
 * @param mode How to handle conflicts. See SHMWaitMode.
 * @param deadline Absolute time to stop waiting, or NULL to wait forever. Ignored for
 * SHMWaitNonBlocking.
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
 * @brief Takes the mapping back from the device once the transfer has completed or
 * the hardware is confirmed stopped.
 *
 * Only the completed part of the range given to begin is copied back, counted from
 * the start of that range. A count of zero ends a cancelled transfer without
 * copying back any received bytes. Does not detect completion or stop the hardware.
 * Makes one syscall, with no request to a service and no allocation.
 *
 * @param mapping Streaming mapping with an active device transfer.
 * @param completedLength Number of completed bytes, counted from the start of the
 *                        range. At most the length of the active range.
 * @return OS_EOK, or an argument, state or kernel error. On failure the transfer
 *         stays active with its current range, so the call can be retried.
 */
DDKDECL(oserr_t,
DMAEndTransfer(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             completedLength));

/**
 * @brief Releases an idle streaming or persistent mapping. If it fails, the mapping
 * is kept. It never stops the hardware, copies data back or removes the caller's
 * SHM attachment. Do not call it on copies of the mapping pointer. After a
 * successful unmap the segment list is no longer valid. CPU sessions stay valid
 * without the mapping.
 * @param mapping Mapping slot, cleared on success. NULL or an empty slot does
 * nothing.
 * @return OS_EOK, OS_EBUSY while device access is active or queued, or a kernel error.
 */
DDKDECL(oserr_t,
DMAUnmap(
    _InOut_ struct DMAMapping** mapping));

_CODE_END
#endif //!__DDK_DMA_H__
