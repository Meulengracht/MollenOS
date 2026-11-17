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
 * Kernel interface for device DMA mappings and contexts over shared-memory
 * (SHM) buffers. Handles mapping buffer ranges for devices, CPU cache syncing,
 * bounce pages, device-pool allocations, and recovery-group revocation.
 */

#ifndef __SHM_DEVICE_H__
#define __SHM_DEVICE_H__

#include <os/osdefs.h>
#include <os/types/shm.h>
#include <os/types/dma.h>
#include <os/types/dma_attachment.h>
#include <os/types/async.h>
#include <os/types/time.h>
#include <shm/access.h>

// Opaque types
DECL_STRUCT(SHMDeviceContext);
DECL_STRUCT(SHMDeviceMapping);
DECL_STRUCT(SHMRecord);

/**
 * @brief Tracks an active operation on a context or mapping record.
 *
 * Every published context is attached to a DMA manager binding. The binding is
 * the link between a driver process and the device it controls. Get a token
 * from SHMDeviceContextBeginOperation or SHMDeviceMappingBeginOperation. This
 * lets the binding's recovery group know that work is running, so it waits for
 * that work before it frees anything. Pass the token to every device call.
 * Keep it until the results have been copied to the caller and any failure
 * cleanup is done, then call SHMDeviceOperationEnd. A NULL token is never
 * accepted for work on a live object.
 */
typedef struct SHMDeviceOperation SHMDeviceOperation_t;

/**
 * @brief How a mapping uses buffer pages and handles CPU caches.
 */
enum SHMDeviceMappingKind {
    // One transfer at a time. Use the original pages when they satisfy device
    // requirements. Otherwise copy through reachable temporary bounce pages.
    SHMDeviceMappingStreamingBounce,
    // One transfer at a time. The device works on the original SHM pages.
    SHMDeviceMappingStreamingDirect,
    // The device works on the original pages. A use count lets several jobs
    // overlap. Cache syncing always covers the whole mapped range.
    SHMDeviceMappingPersistent,
    // Pages from the device pool that the CPU and the device both use at the
    // same time. No sync calls are needed.
    SHMDeviceMappingShared,
    // Fixed regions inside the buffer, each tracked by its own access token.
    // Never uses bounce pages.
    SHMDeviceMappingRegions
};

/**
 * @brief What an operation token is allowed to do.
 */
enum SHMDeviceOperationKind {
    // Start device work or create something new (a mapping, a transfer, a use).
    SHMDeviceOperationSubmit,
    // Finish device work that was started earlier.
    SHMDeviceOperationComplete,
    // Release a mapping.
    SHMDeviceOperationRelease
};

/**
 * @brief Add one reference to a device context.
 *
 * The caller must already hold a reference, and must keep it until this call
 * returns.
 *
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL.
 *         OS_EOVERFLOW if the reference count is already at its maximum.
 *         A failed call adds no reference.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextAcquire(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Release one context reference and set the caller's pointer to NULL.
 *
 * The context is freed when the last reference is released.
 *
 * @param context Pointer to the reference to release. If this is NULL, or points
 *                to a NULL pointer, nothing happens.
 */
KERNELABI void KERNELAPI
SHMDeviceContextRelease(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Closes a context after all of its mappings have been released.
 *
 * The caller must stop new operations on the context before calling this and
 * until it returns. A recovery pass may still hold a short-lived reference. If
 * so, the final cleanup is delayed and the binding stays attached for a while
 * even though the close succeeded.
 *
 * @param context Must point to a valid context. Set to NULL on success.
 * @return OS_EOK on success, or OS_EBUSY if a mapping still exists. On failure
 *         the pointer is left as it was.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextClose(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Prepare a range of an SHM buffer so a device can use it.
 *
 * A mapping holds a reference on the buffer and the context. The buffer's pages
 * stay allocated even if every process unmaps the buffer from its own address
 * space. Creating a mapping does not touch the buffer contents, does not flush
 * or invalidate CPU caches, and does not start a transfer. Only kernel code that
 * has already checked the caller's permission for this device should call it.
 *
 * Only SHM-managed buffers are allowed. Every page in the range must already be
 * allocated (for example by using SHM_COMMIT), because this call never
 * allocates pages on demand.
 *
 * StreamingBounce permits zeroed temporary pages (bounce pages) when the
 * original pages cannot satisfy the device requirements. The sync calls copy
 * bytes between the two. Use it when the device cannot reach the buffer's
 * physical addresses, or when the segments do not meet the alignment rules.
 * Use it too when a device that does not keep the CPU cache up to date
 * (non-coherent) writes to a range that starts or ends in the middle of a CPU
 * cache line. Invalidating that cache line would throw away CPU writes to the
 * neighboring bytes. Uncached device-pool buffers do not have this problem.
 * Normal SHM buffers are cached, so they do.
 *
 * StreamingDirect, Persistent and Shared work on the original pages. On a
 * streaming mapping, call SHMDeviceSyncForDevice before each transfer and
 * SHMDeviceSyncForCpu after it. On a persistent mapping, call SHMDeviceBeginUse
 * before each job and SHMDeviceEndUse after it. The cache is synced over the
 * whole mapped range on the first begin and on the last end.
 *
 * Range conflict checks are widened to whole cache lines. Several readers can
 * share a range, but a write conflicts with any overlapping use. If you submit
 * several jobs through one mapping, make sure the device runs them in a safe
 * order. Never run two calls on the same mapping at the same time.
 *
 * To read or write the buffer from the CPU, create a CPU session with
 * SHMAccessCreate. Wrap the reads and writes in SHMAccessBegin and
 * SHMAccessEnd, and add CPU memory barriers. A Shared mapping only accepts
 * pool pages that are uncached, or that belong to a device that keeps the CPU
 * cache up to date (cache-coherent). SHMDeviceAllocate sets this up for you.
 *
 * Regions mappings work only on buffers created with SHM_REGIONS whose layout
 * has already been registered. The mapped range must cover one or more whole
 * regions from start to end with no gaps. Transfers then run per region through
 * device region sessions instead of SHMDeviceSyncForDevice or SHMDeviceBeginUse,
 * and bounce pages are never used.
 *
 * @param operation Submit token for this context. Keep it until failure cleanup
 *                  is done.
 * @param context Context. Hold a reference until the call returns.
 * @param shmID Buffer to use.
 * @param offset First byte of the range, counted from the start of the buffer.
 * @param length Nonzero number of bytes. The range must fit in the buffer.
 * @param direction Which way data will move for this mapping.
 * @param kind How the mapping uses buffer pages and handles CPU caches.
 * @param requirements Optional limits on each device segment. A zero field
 *                     means no limit. Nonzero values must be powers of two.
 *                     The mapped length must be a multiple of LengthAlignment,
 *                     and the kernel never pads it. Only StreamingBounce can
 *                     meet these limits by copying.
 * @param mappingOut Address of a pointer that is NULL on entry. Receives the
 *                   mapping, which the caller must release.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments, a bad range,
 *         an unknown direction or kind, invalid requirements, or a missing or
 *         wrong operation token.
 *         OS_EBUSY if *mappingOut is not NULL. OS_ENOENT for an unknown buffer,
 *         or for bytes the device cannot address, even through bounce pages.
 *         OS_ENOTSUPPORTED if sharing a cache line directly would be unsafe, if
 *         the alignment cannot be met, or if the storage cannot be shared.
 *         OS_EINCOMPLETE if a page in the range was never allocated.
 *         OS_EOVERFLOW if the context cannot take another reference.
 *         OS_EOOM if an allocation fails.
 *         OS_ECANCELLED if the binding closes during setup.
 *         If the call fails while the mapping is still being prepared, the
 *         temporary resources are freed. Once the mapping has been linked so
 *         that it can be cancelled, any error blocks the recovery group. The
 *         mapping and its pages are then kept until the hardware is confirmed
 *         stopped and the device addresses are released. This includes errors
 *         while installing the addresses. Those are never retried with bounce
 *         pages. Failure leaves *mappingOut unchanged.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMap(
    _In_    SHMDeviceOperation_t*            operation,
    _In_    SHMDeviceContext_t*              context,
    _In_    uuid_t                           shmID,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ SHMDeviceMapping_t**             mappingOut);

/**
 * @brief Add one persistent use before submitting work to the device.
 *
 * Calls on one mapping must not overlap. Do not read or write the mapped range
 * from the CPU until every device use has ended.
 *
 * How the call waits follows SHMWaitMode. OS_EFORKED means a kernel worker
 * thread has taken over the wait. Keep the mapping and the operation token
 * valid until that worker thread finishes.
 *
 * @param operation Mapping Submit token.
 * @param mapping   Persistent mapping to use.
 * @param wait      Wait options, or NULL to fail right away if busy.
 * @return OS_EOK or OS_EFORKED. OS_EINVALPARAMS for NULL, a mapping of a
 *         different kind, or an invalid operation token. OS_EOVERFLOW if the
 *         use count is already at its maximum.
 *         OS_EBUSY if running uses must finish before a conflicting queued
 *         request can start, or if cleanup has begun.
 *         OS_ECANCELLED if the binding was cancelled. The range stays blocked
 *         for the CPU until the hardware is confirmed stopped and cleanup is
 *         done.
 *         Errors from SHMAccessBegin and SHMAccessExtend are returned as they
 *         are.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceBeginUse(
    _In_ SHMDeviceOperation_t*       operation,
    _In_ SHMDeviceMapping_t*         mapping,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief End one persistent use after the device work has finished.
 *
 * Call this only after the transfer has completed, after the hardware is
 * confirmed stopped, or when the job was cancelled before it was submitted.
 *
 * @param operation Mapping Complete token.
 * @param mapping   Persistent mapping with an active use. Calls on one mapping
 *                  must not overlap. The CPU stays blocked from the range until
 *                  the last use ends.
 * @return OS_EOK. OS_EINVALPARAMS for a mapping of a different kind, a mapping
 *         with no active use, or an invalid token. OS_ECANCELLED if stop
 *         cleanup already ran.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceEndUse(
    _In_ SHMDeviceOperation_t* operation,
    _In_ SHMDeviceMapping_t*   mapping);

/**
 * @brief Allocate memory that the CPU and the device use at the same time.
 *
 * Use this for long-lived structures shared with a device, such as command
 * rings. The new SHM buffer is zeroed and follows the device pool's cache
 * policy (uncached on ARM64). Neither side has to sync caches before reading
 * what the other side wrote. The mapping allows reads and writes in both
 * directions. Both sides use the same pages, since a copy could not keep up
 * with both sides writing at once. The device segments cover the allocation
 * rounded up to whole pages.
 *
 * The pages come from the reserved device pool and are physically contiguous.
 * The pool checks the alignment and boundary requirements, and the resulting
 * device segments are checked again before the device may use them. The call
 * fails if there is no pool, if the pool is full, or if the device addresses
 * cannot meet the requirements. Ordinary memory is never used as a fallback.
 *
 * The buffer is created in the calling process and mapped there. To release it,
 * call SHMDeviceUnmap on the mapping first, then SHMDetach on the buffer.
 *
 * @param operation Context Submit token. Keep it until the results are copied
 *                  out or failure cleanup is done.
 * @param context Context. Hold a reference until the call returns.
 * @param requirements Memory limits. See DMARequirements.
 * @param bufferOut Receives the buffer handle, including the CPU pointer.
 * @param mappingOut Address of a pointer that is NULL on entry. Receives the
 *                   mapping, which the caller must release.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments, requirements
 *         that no memory could meet, or an invalid token. OS_EBUSY if
 *         *mappingOut is not NULL. OS_ENOENT if the device cannot reach the
 *         memory. OS_ENOTSUPPORTED if the memory did not meet the requirements.
 *         Errors from creating the buffer are returned as they are.
 *         On failure both outputs are left as they were. If the requirements
 *         are not met, the failure happens before the mapping is linked, and
 *         everything created so far is freed. A failure after linking blocks
 *         the recovery group and keeps the mapping's pages until the hardware
 *         is confirmed stopped, the same as for SHMDeviceMap. In that case the
 *         CPU view of the new buffer is still detached.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceAllocate(
    _In_    SHMDeviceOperation_t*         operation,
    _In_    SHMDeviceContext_t*           context,
    _In_    const struct DMARequirements* requirements,
    _Out_   SHMHandle_t*                  bufferOut,
    _InOut_ SHMDeviceMapping_t**          mappingOut);

/**
 * @brief Prepare a streaming mapping for a device transfer.
 *
 * Reserves the range for the device and makes earlier CPU writes visible to it.
 * If the mapping uses bounce pages and the device will read the data, the range
 * is first copied into the bounce pages. For a device that does not keep the
 * CPU cache up to date (non-coherent), the CPU cache lines are written back to
 * RAM. Before a device write they are also invalidated, so that old cached bytes
 * cannot overwrite what the device wrote later. Uncached pool memory skips the
 * cache calls, but it still runs memory barriers, access checks and bounce
 * copies.
 *
 * The mapping must be idle and the range must not be empty. Calls on one mapping
 * must not overlap. End any CPU session that conflicts with the range before
 * calling. Keep the mapping valid until SHMDeviceSyncForCpu succeeds. Then begin
 * a CPU session before touching the data.
 *
 * How the call waits follows SHMWaitMode. OS_EFORKED means a kernel worker
 * thread has taken over the wait. Keep the mapping and the operation token
 * valid until that worker thread finishes.
 *
 * @param operation Mapping Submit token.
 * @param mapping   Streaming mapping to submit.
 * @param offset    First byte the device will use, counted from the mapping.
 * @param length    Nonzero number of bytes. Must lie inside the mapping.
 * @param wait      Wait options, or NULL to fail right away if busy.
 * @return OS_EOK or OS_EFORKED. OS_EINVALPARAMS for NULL, a mapping that is not
 *         a streaming one, an invalid range, or an invalid operation token.
 *         OS_EBUSY if the mapping is already active or cleanup has begun.
 *         OS_ECANCELLED if the binding was cancelled. The range then stays
 *         blocked for the CPU until the hardware is confirmed stopped and
 *         cleanup is done.
 *         Errors from SHMAccessBegin are returned as they are.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceSyncForDevice(
    _In_ SHMDeviceOperation_t*       operation,
    _In_ SHMDeviceMapping_t*         mapping,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief Make finished device writes visible to the CPU and end device access.
 *
 * Before calling this, confirm that the device really finished. Use a
 * completion interrupt or event, or stop the hardware. For a non-coherent
 * device write, the cached CPU lines for the completed range are discarded. If
 * bounce pages were used, the completed bytes are copied back to the buffer and
 * the bytes outside that range are left alone. Device access then ends. Begin a
 * CPU session before reading or writing the buffer, and treat only the completed
 * range as device output.
 *
 * @param operation Mapping Complete token.
 * @param mapping   Streaming mapping. Shared and persistent mappings are
 *                  rejected. Calls on one mapping must not overlap.
 * @param length    Number of completed bytes, counted from the start of the
 *                  submitted range. At most the submitted length. Passing 0
 *                  cancels the transfer and gives the range back to the CPU.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL, a mapping that is not a
 *         streaming one, a bad range, an invalid token, or a mapping with no
 *         active transfer. OS_ECANCELLED if stop cleanup already ran.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceSyncForCpu(
    _In_ SHMDeviceOperation_t* operation,
    _In_ SHMDeviceMapping_t*   mapping,
    _In_ size_t                length);

/**
 * @brief Release a mapping and its references on the buffer and the context.
 *
 * Finish or stop all device work first. For a streaming mapping, call
 * SHMDeviceSyncForCpu before unmapping. For a persistent mapping, end every use
 * with SHMDeviceEndUse. No device access may still be queued. This call does
 * not stop the hardware.
 *
 * @param operation Mapping Release token. When cleaning up after a failed
 *                  mapping creation, pass the context Submit token instead.
 *                  End the token after this call. A NULL token is accepted only
 *                  when mapping or *mapping is NULL.
 * @param mapping   Pointer that is set to NULL on success. If this is NULL, or
 *                  points to a NULL pointer, nothing happens. Calls on one
 *                  mapping must not overlap.
 * @return OS_EOK, OS_EINVALPARAMS for an invalid token, or OS_EBUSY while
 *         device work is active. If freeing the device addresses fails, the
 *         pointer is left as it was and the pages stay allocated so cleanup
 *         can try again. The mapping can no longer be used for transfers or
 *         segment queries after that.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceUnmap(
    _In_    SHMDeviceOperation_t* operation,
    _InOut_ SHMDeviceMapping_t**  mapping);

/**
 * @brief Read the device segments covering the mapped range, in order.
 *
 * @param operation Mapping Submit token, or the context Submit token while a
 *                  mapping is being created. Keep it valid for as long as you
 *                  read the returned segments.
 * @param mapping   Valid mapping. Release must not overlap this call.
 * @param countOut  Receives the number of segments.
 * @return The segment list, which stays valid until release begins. NULL if an
 *         argument is NULL. An invalid token, or a release that has already
 *         begun, also returns NULL and sets *countOut to 0. The backing memory
 *         stays pinned until the token ends.
 */
KERNELABI KERNELAPI const struct DMASegment*
SHMDeviceMappingSegments(
    _In_  SHMDeviceOperation_t*     operation,
    _In_  const SHMDeviceMapping_t* mapping,
    _Out_ uint32_t*                 countOut);

/**
 * @brief Cleans up a mapping after the process that holds its handle exits.
 *
 * Shuts down the whole recovery group, which is the set of devices and contexts
 * that are stopped and recovered together. A mapping that still has device
 * resources stays on the context's list, and so does an idle one. Those
 * resources are only released once the hardware is confirmed stopped.
 *
 * @param mapping Must point to a mapping pointer. Set to NULL once the mapping
 *                is freed or handed to the context's stop cleanup.
 * @return OS_EOK once the mapping is freed or handed to stop cleanup.
 */
oserr_t
SHMDeviceAbandon(
    _InOut_ SHMDeviceMapping_t** mapping);

/**
 * @brief Cancels waiting requests and blocks the ranges of all mappings in a context.
 *
 * Call this after the recovery group has already stopped new DMA work. A mapping
 * that is still being created blocks its own range when it is added to the list.
 *
 * @param context Context whose mapping list stays valid during the walk.
 * @return OS_EOK if every mapping was visited. OS_EBUSY or OS_EOVERFLOW means
 *         some work is still not blocked, so call this again.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextRevoke(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Releases mapping resources after the hardware has stopped.
 *
 * The recovery group calls this while it holds a context reference. Before that,
 * its stop ticket must confirm that the hardware can no longer touch RAM, and
 * every operation that was let in must have finished. This visits both live and
 * abandoned mappings. If freeing a device address fails, the pages and the range
 * protection are kept so cleanup can try again.
 *
 * @param context Context whose device has stopped accessing memory.
 * @return OS_EOK, or the first reference, busy or address-release error.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextCompleteStop(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Prepares a bound device context from a driver's binding handle and generation.
 *
 * Fills in the context struct before it is attached to the binding. Then it
 * builds the fixed address rules for direct DMA, without holding the group
 * lock. On error, *contextOut is left as it was.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextPrepareBound(
    _In_  uuid_t                            binding,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _Out_ SHMDeviceContext_t**              contextOut);

/**
 * @brief Finishes publishing a bound context after its handle was copied to user space.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextPublishBound(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Cancels a bound context that was never published. It must have no mappings.
 *
 * Sets *context to NULL. The operation count taken during setup is dropped when
 * the last short-lived reference is released.
 */
KERNELABI void KERNELAPI
SHMDeviceContextCancelBound(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Cleans up a device context after every thread of its process has exited.
 *
 * If any mappings still exist, this shuts down the whole recovery group before
 * it drops the context reference. A recovery pass that holds a short-lived
 * reference releases it with SHMDeviceContextRelease.
 */
KERNELABI void KERNELAPI
SHMDeviceContextAbandon(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Starts an operation, using a token stored inside the context struct.
 *
 * The record must be in the Updating state and its PendingOperation slot must be
 * empty. The token lives inside the context struct, so this call never
 * allocates memory. Keep the token until setup, the copies to user space and any
 * failure cleanup are done, then end it with SHMDeviceOperationEnd. If a wait
 * returns OS_EFORKED, the kernel worker thread keeps the token and ends it when
 * it finishes.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextBeginOperation(
    _In_  SHMRecord_t*                record,
    _In_  SHMDeviceContext_t*         context,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut);

/**
 * @brief Starts an operation, using a token stored inside the mapping struct.
 *
 * The record and token rules are the same as for SHMDeviceContextBeginOperation.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingBeginOperation(
    _In_  SHMRecord_t*                record,
    _In_  SHMDeviceMapping_t*         mapping,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut);

/**
 * @brief Checks that a bound context's binding is still live before work is committed.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceOperationCheck(
    _In_ const SHMDeviceOperation_t* operation);

/**
 * @brief Clears the token and the record slot, lowers the group's operation count and drops references.
 */
KERNELABI void KERNELAPI
SHMDeviceOperationEnd(
    _InOut_ SHMDeviceOperation_t** operation);

/**
 * @brief Cancels a blocked range wait when the process that started it exits.
 *
 * Call this only after every thread in the process has stopped, including any
 * background worker that is waiting. If the operation was interrupted somewhere
 * other than a range wait (for example while copying data or flushing caches),
 * this returns OS_EBUSY. It keeps the operation count and the pinned pages,
 * because there is no way to tell what state the operation left behind.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceOperationCancel(
    _In_ SHMRecord_t* record);

#endif //!__SHM_DEVICE_H__
