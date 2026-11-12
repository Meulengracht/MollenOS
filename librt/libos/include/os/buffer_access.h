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

#ifndef __OS_BUFFER_ACCESS_H__
#define __OS_BUFFER_ACCESS_H__

#include <os/fence_pool.h>

_CODE_BEGIN

// Opaque forward declaration of internal data
struct __OSBufferReservation;

/**
 * @brief Shared identity for one buffer region.
 *
 * Buffer identifies the shared buffer. Region is an agreed region number, which
 * can be zero. Callers agree on its byte range separately. Generation distinguishes
 * different uses of the identity. Buffer must not be UUID_INVALID and Generation
 * must be nonzero. Initialize the whole structure, including padding, before
 * setting fields. Binding validation compares the identity bytes with the local copy.
 *
 * All aliases of a region must use the same shared reservation record. Separate
 * regions must not share cache lines. The caller must check aliases, byte ranges
 * and buffer IDs. Keep the identity unchanged until all users and references to
 * the region finish. Calls documented as making no syscalls may still write
 * errors or enabled traces to the system log.
 */
typedef struct OSBufferIdentity {
    uuid_t       Buffer;
    unsigned int Region;
    unsigned int Generation;
} OSBufferIdentity_t;

/**
 * @brief Local binding to the shared access record for a buffer region.
 *
 * OSBufferReservationBind sets the identity, reader limit and local storage
 * address. These fields must not be edited or sent to another process. Keep the
 * binding and mapping valid during publish and reclaim calls, and while any job's
 * scratch array uses the binding. Storage holds counted pool references for the
 * last writer and a fixed number of readers.
 *
 * All users of the buffer must follow this protocol and the kernel's CPU/DMA
 * access rules. Reservations order jobs but do not prevent CPU or device access
 * to the bytes. Writing directly to reservation storage can break the ordering.
 */
typedef struct OSBufferReservation {
    OSBufferIdentity_t            Identity;
    unsigned int                  ReaderCapacity;
    struct __OSBufferReservation* Storage;
} OSBufferReservation_t;

/**
 * @brief Buffer access required by a job.
 *
 * Reads wait for the previous writer and may run with other reads. Write and
 * overwrite wait for the previous writer and all recorded readers. Read and write
 * require valid previous contents. Overwrite still waits for the writer to finish,
 * but may replace data left invalid by a failed writer.
 */
enum OSBufferAccessMode {
    // Reads existing contents. A previous writer's error is passed to this job.
    OSBufferAccessRead,
    // Writes may use existing contents, so a previous writer's error is passed on.
    OSBufferAccessWrite,
    // Replaces the whole region after earlier accesses end. Previous data may be invalid.
    OSBufferAccessOverwrite
};

/**
 * @brief Buffer region and access mode in a job's request or scratch array.
 *
 * Duplicate identities are merged before publication. Equal modes stay unchanged.
 * Different modes become OSBufferAccessWrite, since combining read with overwrite
 * still requires valid previous contents.
 */
typedef struct OSBufferAccessRequest {
    OSBufferReservation_t*  Buffer;
    enum OSBufferAccessMode Mode;
} OSBufferAccessRequest_t;

/**
 * @brief Counted reference to earlier work that this job must wait for.
 *
 * NeedsContents is true for a previous writer whose data this job needs. Its
 * failure becomes the job's dependency error. Other dependencies only order
 * access. Poll releases completed references and clears their Fence fields.
 * Callers must not signal, release or edit these entries.
 */
typedef struct OSBufferDependency {
    OSFenceReference_t Fence;
    bool               NeedsContents;
} OSBufferDependency_t;

/**
 * @brief Caller-allocated arrays for buffer requests and dependencies.
 *
 * Buffers must hold the original request count, before duplicates are merged.
 * Dependencies must hold the pending references from each unique region. A read
 * needs at most one writer entry. Write and overwrite need at most one writer
 * plus ReaderCapacity readers. Add these limits for all unique regions when
 * sizing the array. The same fence can appear in several entries.
 *
 * Capacities are element counts. Dependencies may be NULL with zero capacity
 * only if no pending references need to be stored. Each active job needs separate
 * arrays. They must not overlap each other, requests, the job, the completion
 * argument or shared storage. Do not modify them until finish succeeds. A failed
 * publish may change the arrays but leaves the job inactive. The caller must
 * allocate and resize the arrays.
 */
typedef struct OSBufferAccessScratch {
    OSBufferAccessRequest_t* Buffers;
    size_t                   BufferCapacity;
    OSBufferDependency_t*    Dependencies;
    size_t                   DependencyCapacity;
} OSBufferAccessScratch_t;

/**
 * @brief Local job state with its completion and dependencies.
 *
 * Zero the whole record, then set Scratch before first use. All other members
 * are private to libos. Calls on a job must be serialized. Do not copy a published
 * job or signal or release Completion directly. Pools, mappings and scratch
 * arrays must stay valid until finish succeeds.
 *
 * Completion holds the producer reference moved from TryPublish's input.
 * BufferCount counts unique requests. DependencyCount includes entries already
 * cleared by Poll. DependencyResult stores the first observed error from a writer
 * whose contents were needed. If several writers fail, observation order decides
 * which error is returned.
 *
 * Take a pool completion and publish the accesses. Poll until all dependencies
 * end and check their result before starting CPU/DMA access. If a required writer
 * failed, do not start the work. End CPU/DMA access before finishing the job.
 * Cancelled or rejected jobs must also wait for their dependencies, as later jobs
 * may already be waiting on this completion. Finish clears the job on success
 * and keeps Scratch for the next use.
 */
typedef struct OSBufferAccessJob {
    OSBufferAccessScratch_t Scratch;
    const OSFencePool_t*    Pools;
    unsigned int            PoolCount;
    OSFenceReference_t      Completion;
    size_t                  BufferCount;
    size_t                  DependencyCount;
    oserr_t                 DependencyResult;
} OSBufferAccessJob_t;

/**
 * @brief Calculates reservation storage size for a fixed reader limit.
 *
 * Includes the header, one writer reference and readerCapacity reader slots.
 * Does not access memory. The reader limit is fixed while the storage is in use.
 * Storage must be aligned to _Alignof(max_align_t). All processes must use the
 * same ABI.
 *
 * @param readerCapacity Nonzero reader limit.
 * @return Required bytes, or zero for an invalid size or overflow.
 */
CRTDECL(size_t,
OSBufferReservationSize(
    _In_ unsigned int readerCapacity));

/**
 * @brief Initializes unused reservation storage before sharing it.
 *
 * No other user may access the storage during initialization or teardown. The
 * caller must ensure that aliases share a record and separate regions do not
 * share cache lines. Initially there are no readers or writer, and the contents
 * are treated as valid (OS_EOK). Set up the buffer contents before allowing jobs
 * to use them. Share the SHM ID and offset after initialization so peers can bind
 * their mappings. Does not allocate or map memory.
 *
 * @param storage Committed, cached, read/write shared memory aligned to
 *                _Alignof(max_align_t).
 * @param size Available bytes, at least OSBufferReservationSize(readerCapacity).
 * @param identity Valid buffer ID and nonzero generation, stable until teardown.
 *                 Keep the identity argument outside the storage being initialized.
 * @param readerCapacity Fixed nonzero reader limit.
 * @return OS_EOK, or OS_EINVALPARAMS for invalid storage, alignment, size, identity
 *         or reader capacity.
 */
CRTDECL(oserr_t,
OSBufferReservationInitialize(
    _Out_ void*                     storage,
    _In_  size_t                    size,
    _In_  const OSBufferIdentity_t* identity,
    _In_  unsigned int              readerCapacity));

/**
 * @brief Binds initialized reservation storage at the local mapped address.
 *
 * Version, capacity and identity must stay unchanged while the record is in use.
 * Copies the identity and sets local pointers. Does not map SHM, add a mapping
 * reference or reset access records.
 *
 * @param storage Initialized shared storage. Layout and identity must not change.
 * @param size Mapped bytes available from storage, covering the whole recorded layout.
 * @param bufferOut Local output, separate from shared storage. Unchanged on error.
 *                  Do not overwrite a binding still referenced by a job.
 * @return OS_EOK, or OS_EINVALPARAMS for NULL arguments or invalid layout, version,
 *         alignment or identity.
 */
CRTDECL(oserr_t,
OSBufferReservationBind(
    _In_  void*                  storage,
    _In_  size_t                 size,
    _Out_ OSBufferReservation_t* bufferOut));

/**
 * @brief Records dependencies and publishes all accesses for a job together.
 *
 * Completion must come from OSFencePoolTryTake and must not have been submitted.
 * All pools used by the completion or reservation entries must be bound. Each
 * region needs a valid binding and must follow the identity and alias rules above.
 * Calls on the job and its producer reference must be serialized. Completion must
 * not overlap job->Completion.
 *
 * Sorts and merges requests, tries each reservation lock once, then records the
 * earlier work that conflicts with this job. Submits the completion and adds a
 * counted reference to each reservation before releasing the locks. On failure,
 * reservation entries and the input completion stay unchanged. Temporary
 * references are released, but scratch array contents may have changed.
 *
 * After success, later jobs can depend on this completion. Before accessing data,
 * call OSBufferAccessPoll until all dependencies end, check their result and begin
 * the required kernel CPU/DMA access. Does not allocate, make IPC or syscalls, or
 * wait. The caller decides when to retry OS_EBUSY.
 *
 * @param pools Binding array. Must stay valid. Each bound pool's ID equals its index.
 * @param poolCount Binding count. Every referenced pool must already be bound.
 * @param requests Nonempty access list. Duplicate modes merge as described above.
 *                 Separate input from job scratch and all shared storage.
 * @param count Input count, bounded by Scratch.BufferCapacity before merging.
 * @param completion Taken, unsubmitted producer reference. Moved into job and
 *                   cleared on success. Use OSFencePoolAcquire for a separate
 *                   consumer and expose it for polling only after publish succeeds.
 * @param job Empty record with configured scratch. Unchanged on failure, except
 *            for scratch array contents.
 * @return OS_EOK, OS_EBUSY for lock contention, a full reader set, an active job or
 *         an already submitted completion, OS_EBUFFER for insufficient scratch,
 *         OS_EINVALPARAMS for invalid arguments, modes or references, OS_ENOENT for
 *         changed reservation metadata or old references, or OS_EOVERFLOW if a
 *         reference count cannot be increased.
 */
CRTDECL(oserr_t,
OSBufferAccessTryPublish(
    _In_    const OSFencePool_t*           pools,
    _In_    unsigned int                   poolCount,
    _In_    const OSBufferAccessRequest_t* requests,
    _In_    size_t                         count,
    _InOut_ OSFenceReference_t*            completion,
    _InOut_ OSBufferAccessJob_t*           job));

/**
 * @brief Polls job dependencies without allocation or a syscall.
 *
 * OS_EOK means all dependencies ended. resultOut reports whether the required
 * input data is valid. For example, *resultOut == OS_EDEVFAULT means a required
 * writer failed. Does not start or signal the job. Completed dependencies may be
 * released while others are still pending, or before a later entry returns an
 * error. Retry on the same job.
 *
 * @param job Published job. Completed references are released after observation.
 * @param resultOut First failed required writer result, or OS_EOK. Unchanged
 *                  until every dependency ends. Failed readers do not make the
 *                  buffer contents invalid.
 * @return OS_EOK when all dependencies end, OS_EINPROGRESS if any are pending,
 *         OS_EINVALPARAMS for invalid arguments or an unpublished job, or a pool
 *         reference/state error. resultOut is unchanged unless OS_EOK is returned.
 */
CRTDECL(oserr_t,
OSBufferAccessPoll(
    _InOut_ OSBufferAccessJob_t* job,
    _Out_   oserr_t*             resultOut));

/**
 * @brief Finishes a published job after CPU/DMA access has ended.
 *
 * Hardware must have finished or be confirmed stopped, and required kernel
 * end-access calls must have succeeded. Finish does neither. Remaining dependencies
 * are polled, also when cancelling. Ending the job before its dependencies could
 * let later jobs access data that an earlier job still uses.
 *
 * Once dependencies end, an error result is used as supplied. For OS_EOK, any
 * required writer's error is passed on. Consumers can see completion before waiter
 * notification finishes. If notification fails, the job and producer reference
 * stay valid for retry. Use the same result and do not repeat the device work.
 * Shared reservation references stay until replaced or reclaimed.
 *
 * @param job Published job. All dependencies must end, also on cancellation.
 * @param result Job result. OS_EOK propagates a failed required writer result.
 * @return OS_EOK with an empty reusable job, OS_EINPROGRESS while dependencies
 *         remain, or a dependency or notification error. Errors leave the job
 *         available for retry.
 */
CRTDECL(oserr_t,
OSBufferAccessFinish(
    _InOut_ OSBufferAccessJob_t* job,
    _In_    oserr_t              result));

/**
 * @brief Removes completed references and saves the writer's result in the record.
 *
 * Completed references keep pool slots in use until replaced or reclaimed. The
 * writer's result is saved so later reads and writes still see its error after
 * the completion slot is reused. Reader results are discarded because readers
 * do not change buffer contents.
 *
 * Pending entries may remain after OS_EOK. Before teardown, prevent new
 * publications, finish all jobs, release external completion references and
 * reclaim the records while pool mappings are still valid. All processes must
 * finish these steps. Process exit does not clean up references.
 *
 * @param pools Stable binding array for all table references.
 * @param poolCount Binding count.
 * @param buffer Live binding. May run while jobs are active. Never clears pending
 *               accesses. Reclaim all records after jobs end before teardown.
 * @return OS_EOK, OS_EBUSY for lock contention, or reference error. No allocation,
 *         wait or syscall occurs. A reference error may leave some entries cleared.
 */
CRTDECL(oserr_t,
OSBufferReservationReclaim(
    _In_ const OSFencePool_t*   pools,
    _In_ unsigned int           poolCount,
    _In_ OSBufferReservation_t* buffer));

_CODE_END
#endif //!__OS_BUFFER_ACCESS_H__
