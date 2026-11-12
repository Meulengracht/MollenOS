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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <os/buffer_access.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define BUFFER_RESERVATION_VERSION 1U

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Shared reservations require lock-free integer atomics");

/**
 * @brief Shared writer and reader records for one buffer region.
 *
 * Version, Identity and ReaderCapacity are fixed after initialization. Lock is
 * zero when free and one while Writer, WriterResult or Readers are being read
 * or changed. Calls try the lock once and return OS_EBUSY if it is taken.
 *
 * Writer and Readers hold counted pool references. The latest writer may still
 * be waiting for earlier jobs. Once its completed reference is removed,
 * WriterResult keeps its result so later jobs can check whether the data is valid.
 * Reader results are discarded because readers do not change data. Generation
 * zero marks an empty entry. OSBufferReservationSize includes ReaderCapacity
 * entries for the flexible Readers array.
 */
struct __OSBufferReservation {
    unsigned int       Version;
    OSBufferIdentity_t Identity;
    unsigned int       ReaderCapacity;
    atomic_uint        Lock;
    OSFenceReference_t Writer;
    oserr_t            WriterResult;
    OSFenceReference_t Readers[];
};

size_t
OSBufferReservationSize(
    _In_ unsigned int readerCapacity)
{
    size_t count = readerCapacity;

    if (count == 0 ||
        count > (SIZE_MAX - sizeof(struct __OSBufferReservation)) / sizeof(OSFenceReference_t)) {
        return 0;
    }
    return sizeof(struct __OSBufferReservation) + count * sizeof(OSFenceReference_t);
}

static bool
__ReservationStorageValid(
    _In_ void*  storage,
    _In_ size_t size)
{
    return storage != NULL && (uintptr_t)storage % _Alignof(max_align_t) == 0 &&
        size >= sizeof(struct __OSBufferReservation);
}

oserr_t
OSBufferReservationInitialize(
    _Out_ void*                     storage,
    _In_  size_t                    size,
    _In_  const OSBufferIdentity_t* identity,
    _In_  unsigned int              readerCapacity)
{
    struct __OSBufferReservation* reservation = storage;
    size_t                        required = OSBufferReservationSize(readerCapacity);

    if (!__ReservationStorageValid(storage, size) || required == 0 || required > size) {
        WARNING("OSBufferReservationInitialize: invalid storage %p, size %zu or reader capacity %u",
            storage, size, readerCapacity);
        return OS_EINVALPARAMS;
    }
    
    if (identity == NULL || identity->Buffer == UUID_INVALID || identity->Generation == 0) {
        WARNING("OSBufferReservationInitialize: missing or invalid buffer identity");
        return OS_EINVALPARAMS;
    }
    
    // All users must be excluded and identity must be outside this range.
    // Zero clears the references and sets WriterResult to OS_EOK.
    memset(storage, 0, required);
    reservation->Version = BUFFER_RESERVATION_VERSION;
    reservation->Identity = *identity;
    reservation->ReaderCapacity = readerCapacity;
    atomic_init(&reservation->Lock, 0);
    return OS_EOK;
}

oserr_t
OSBufferReservationBind(
    _In_  void*                  storage,
    _In_  size_t                 size,
    _Out_ OSBufferReservation_t* bufferOut)
{
    struct __OSBufferReservation* reservation = storage;
    size_t                        required;

    if (bufferOut == NULL || !__ReservationStorageValid(storage, size)) {
        WARNING("OSBufferReservationBind: invalid storage %p, size %zu or missing output", storage, size);
        return OS_EINVALPARAMS;
    }

    required = OSBufferReservationSize(reservation->ReaderCapacity);
    if (reservation->Version != BUFFER_RESERVATION_VERSION || required == 0 || required > size) {
        WARNING("OSBufferReservationBind: invalid header (version %u, readers %u, size %zu)",
            reservation->Version, reservation->ReaderCapacity, size);
        return OS_EINVALPARAMS;
    }

    if (reservation->Identity.Buffer == UUID_INVALID || reservation->Identity.Generation == 0) {
        WARNING("OSBufferReservationBind: invalid buffer identity");
        return OS_EINVALPARAMS;
    }

    bufferOut->Identity = reservation->Identity;
    bufferOut->ReaderCapacity = reservation->ReaderCapacity;
    bufferOut->Storage = reservation;
    return OS_EOK;
}

static const OSFencePool_t*
__ReferencePool(
    _In_ const OSFencePool_t*      pools,
    _In_ unsigned int              poolCount,
    _In_ const OSFenceReference_t* reference)
{
    // Pool IDs index the caller's local bindings. Addresses from another process
    // cannot be used here.
    if (reference->Pool >= poolCount || pools[reference->Pool].ID != reference->Pool) {
        TRACE("__ReferencePool: pool %u is not bound", reference->Pool);
        return NULL;
    }
    return &pools[reference->Pool];
}

static void
__DropReference(
    _In_    const OSFencePool_t* pools,
    _In_    unsigned int         poolCount,
    _InOut_ OSFenceReference_t*  reference)
{
    const OSFencePool_t* pool;
    oserr_t              status;

    if (reference->Generation == 0) {
        return;
    }

    // The pool must have been validated. Hold the table lock or serialize calls
    // on the local job before releasing this reference. Pending entries must
    // still have a producer or dependency reference, as the last pending reference
    // cannot be released.
    pool = __ReferencePool(pools, poolCount, reference);
    
    status = OSFencePoolRelease(pool, reference);
    if (status != OS_EOK) {
        ERROR("__DropReference: pool %u slot %u generation %u release failed (%u)",
            reference->Pool, reference->Slot, reference->Generation, status);
    }
    assert(status == OS_EOK);
    (void)status;
}

static oserr_t
__PollReference(
    _In_  const OSFencePool_t*      pools,
    _In_  unsigned int              poolCount,
    _In_  const OSFenceReference_t* reference,
    _Out_ oserr_t*                  resultOut)
{
    const OSFencePool_t* pool = __ReferencePool(
        pools,
        poolCount,
        reference
    );
    return OSFencePoolPoll(pool, reference, resultOut);
}

static int
__CompareBuffers(
    _In_ const OSBufferAccessRequest_t* left,
    _In_ const OSBufferAccessRequest_t* right)
{
    const OSBufferIdentity_t* a = &left->Buffer->Identity;
    const OSBufferIdentity_t* b = &right->Buffer->Identity;

    // Sort by shared identity so aliases sort together, also when processes map
    // the same region at different addresses.
    if (a->Buffer != b->Buffer) {
        return a->Buffer < b->Buffer ? -1 : 1;
    }

    if (a->Region != b->Region) {
        return a->Region < b->Region ? -1 : 1;
    }

    if (a->Generation != b->Generation) {
        return a->Generation < b->Generation ? -1 : 1;
    }
    return 0;
}

static void
__SiftBuffers(
    _InOut_ OSBufferAccessRequest_t* buffers,
    _In_    size_t                   root,
    _In_    size_t                   count)
{
    OSBufferAccessRequest_t saved = buffers[root];
    size_t                  child;

    // Move the larger child up until saved fits below root. This restores the
    // heap without extra storage.
    while (root < count / 2) {
        child = root * 2 + 1;
        
        if (child + 1 < count && __CompareBuffers(&buffers[child], &buffers[child + 1]) < 0) {
            child++;
        }

        if (__CompareBuffers(&saved, &buffers[child]) >= 0) {
            break;
        }

        buffers[root] = buffers[child];
        root = child;
    }
    buffers[root] = saved;
}

static void
__SortBuffers(
    _InOut_ OSBufferAccessRequest_t* buffers,
    _In_    size_t                   count)
{
    OSBufferAccessRequest_t saved;

    // Count must be nonzero, checked by TryPublish. Heapsort keeps comparison
    // work bounded and uses no extra allocation or recursive calls.
    for (size_t i = count / 2; i != 0; i--) {
        __SiftBuffers(buffers, i - 1, count);
    }

    for (size_t i = count - 1; i != 0; i--) {
        saved = buffers[0];
        buffers[0] = buffers[i];
        buffers[i] = saved;
        __SiftBuffers(buffers, 0, i);
    }
}

static oserr_t
__PrepareBuffers(
    _In_    const OSBufferAccessRequest_t* requests,
    _In_    size_t                         count,
    _InOut_ OSBufferAccessJob_t*           job)
{
    // Check the fixed metadata before accessing shared entries. Use scratch
    // so the input requests stay unchanged if a later step fails.
    for (size_t i = 0; i < count; i++) {
        OSBufferReservation_t* buffer = requests[i].Buffer;
        if (buffer == NULL || buffer->Storage == NULL) {
            WARNING("__PrepareBuffers: request %zu has no buffer binding", i);
            return OS_EINVALPARAMS;
        }
        
        if (requests[i].Mode < OSBufferAccessRead || requests[i].Mode > OSBufferAccessOverwrite) {
            WARNING("__PrepareBuffers: request %zu has invalid access mode %d", i, requests[i].Mode);
            return OS_EINVALPARAMS;
        }

        if (memcmp(&buffer->Identity, &buffer->Storage->Identity, sizeof(buffer->Identity)) != 0 ||
            buffer->ReaderCapacity != buffer->Storage->ReaderCapacity) {
            WARNING("__PrepareBuffers: request %zu buffer %u region %u generation %u no longer matches storage",
                i, buffer->Identity.Buffer, buffer->Identity.Region, buffer->Identity.Generation);
            return OS_ENOENT;
        }

        job->Scratch.Buffers[i] = requests[i];
    }

    __SortBuffers(job->Scratch.Buffers, count);

    // Equal identities must refer to the same shared record. Merge them so the
    // job takes the lock once and adds only one access to each record.
    for (size_t i = 0; i < count; i++) {
        OSBufferAccessRequest_t* current = &job->Scratch.Buffers[i];
        if (job->BufferCount != 0) {
            OSBufferAccessRequest_t* previous = &job->Scratch.Buffers[job->BufferCount - 1];
            if (__CompareBuffers(previous, current) == 0) {
                // Mixed modes may need previous contents, so they cannot be
                // treated as a full overwrite of invalid data.
                if (previous->Mode != current->Mode) {
                    previous->Mode = OSBufferAccessWrite;
                }
                continue;
            }
        }
        job->Scratch.Buffers[job->BufferCount++] = *current;
    }
    return OS_EOK;
}

static void
__UnlockBuffers(
    _In_ OSBufferAccessJob_t* job,
    _In_ size_t               count)
{
    while (count != 0) {
        count--;
        atomic_store_explicit(
            &job->Scratch.Buffers[count].Buffer->Storage->Lock,
            0,
            memory_order_release
        );
    }
}

static oserr_t
__LockBuffers(
    _In_ OSBufferAccessJob_t* job)
{
    // Take locks in identity order. If one is busy, release the earlier locks
    // immediately so the caller can retry without holding any locks.
    for (size_t i = 0; i < job->BufferCount; i++) {
        unsigned int expected = 0;
        if (!atomic_compare_exchange_strong_explicit(&job->Scratch.Buffers[i].Buffer->Storage->Lock,
            &expected, 1, memory_order_acquire, memory_order_relaxed)) {
            __UnlockBuffers(job, i);
            TRACE("__LockBuffers: buffer %u region %u is busy",
                job->Scratch.Buffers[i].Buffer->Identity.Buffer,
                job->Scratch.Buffers[i].Buffer->Identity.Region);
            return OS_EBUSY;
        }
    }
    return OS_EOK;
}

static oserr_t
__CaptureDependency(
    _InOut_ OSBufferAccessJob_t*      job,
    _In_    const OSFenceReference_t* reference,
    _In_    bool                      needsContents)
{
    OSBufferDependency_t* dependency;
    const OSFencePool_t*  pool;
    oserr_t               result;
    oserr_t               status;

    // The table lock must be held while polling or adding a reference. Completed
    // dependencies need only their result. Pending dependencies need a job
    // reference before the table entry can be replaced.
    status = __PollReference(job->Pools, job->PoolCount, reference, &result);
    if (status == OS_EOK) {
        if (needsContents && job->DependencyResult == OS_EOK) {
            job->DependencyResult = result;
        }
        return OS_EOK;
    }

    if (status != OS_EINPROGRESS) {
        return status;
    }

    if (job->DependencyCount == job->Scratch.DependencyCapacity) {
        TRACE("__CaptureDependency: dependency capacity %zu exhausted", job->Scratch.DependencyCapacity);
        return OS_EBUFFER;
    }

    pool = __ReferencePool(job->Pools, job->PoolCount, reference);
    status = OSFencePoolAcquire(pool, reference);
    if (status != OS_EOK) {
        return status;
    }

    dependency = &job->Scratch.Dependencies[job->DependencyCount++];
    dependency->Fence = *reference;
    dependency->NeedsContents = needsContents;
    return OS_EOK;
}

static oserr_t
__CaptureBuffer(
    _InOut_ OSBufferAccessJob_t*     job,
    _In_    OSBufferAccessRequest_t* request)
{
    struct __OSBufferReservation* buffer = request->Buffer->Storage;
    unsigned int                  activeReaders = 0;
    bool                          needsContents = request->Mode != OSBufferAccessOverwrite;
    oserr_t                       result;
    oserr_t                       status;

    // All accesses wait for the previous writer. A full overwrite can ignore
    // its error because no previous contents are needed.
    if (buffer->Writer.Generation != 0) {
        status = __CaptureDependency(
            job,
            &buffer->Writer,
            needsContents
        );
        if (status != OS_EOK) {
            return status;
        }
    } else if (needsContents && job->DependencyResult == OS_EOK) {
        job->DependencyResult = buffer->WriterResult;
    }

    // Concurrent reads each need a free entry. Writers must wait for all readers,
    // also those that failed.
    for (unsigned int i = 0; i < buffer->ReaderCapacity; i++) {
        if (buffer->Readers[i].Generation == 0) {
            continue;
        }

        if (request->Mode != OSBufferAccessRead) {
            status = __CaptureDependency(job, &buffer->Readers[i], false);
            if (status != OS_EOK) {
                return status;
            }
            continue;
        }

        status = __PollReference(
            job->Pools,
            job->PoolCount,
            &buffer->Readers[i],
            &result
        );
        if (status == OS_EINPROGRESS) {
            activeReaders++;
        } else if (status != OS_EOK) {
            return status;
        }
    }

    if (request->Mode == OSBufferAccessRead && activeReaders == buffer->ReaderCapacity) {
        TRACE("__CaptureBuffer: buffer %u region %u has no available reader entry",
            buffer->Identity.Buffer, buffer->Identity.Region);
        return OS_EBUSY;
    }
    return OS_EOK;
}

static oserr_t
__ReclaimBuffer(
    _In_ const OSFencePool_t*          pools,
    _In_ unsigned int                  poolCount,
    _In_ struct __OSBufferReservation* buffer)
{
    oserr_t result;
    oserr_t status;

    // The reservation lock must be held. Save the writer's result before releasing
    // its slot so later jobs still see an error if the contents are invalid.
    if (buffer->Writer.Generation != 0) {
        status = __PollReference(
            pools,
            poolCount,
            &buffer->Writer,
            &result
        );
        if (status == OS_EOK) {
            buffer->WriterResult = result;
            __DropReference(pools, poolCount, &buffer->Writer);
        } else if (status != OS_EINPROGRESS) {
            return status;
        }
    }

    for (unsigned int i = 0; i < buffer->ReaderCapacity; i++) {
        if (buffer->Readers[i].Generation == 0) {
            continue;
        }
        
        status = __PollReference(
            pools,
            poolCount,
            &buffer->Readers[i],
            &result
        );
        if (status == OS_EOK) {
            __DropReference(pools, poolCount, &buffer->Readers[i]);
        } else if (status != OS_EINPROGRESS) {
            return status;
        }
    }
    return OS_EOK;
}

static void
__PublishBuffer(
    _In_ OSBufferAccessJob_t*     job,
    _In_ OSBufferAccessRequest_t* request)
{
    struct __OSBufferReservation* buffer = request->Buffer->Storage;
    oserr_t                       status;

    if (request->Mode == OSBufferAccessRead) {
        // The locks have been held since validation. Completion can only make
        // more reader slots available, so reclaim cannot fail here.
        status = __ReclaimBuffer(job->Pools, job->PoolCount, buffer);
        if (status != OS_EOK) {
            ERROR("__PublishBuffer: buffer %u region %u reclaim failed after validation (%u)",
                buffer->Identity.Buffer, buffer->Identity.Region, status);
        }
        
        assert(status == OS_EOK);
        (void)status;
        
        for (unsigned int i = 0; i < buffer->ReaderCapacity; i++) {
            if (buffer->Readers[i].Generation == 0) {
                buffer->Readers[i] = job->Completion;
                return;
            }
        }

        ERROR("__PublishBuffer: buffer %u region %u has no reader entry after validation",
            buffer->Identity.Buffer, buffer->Identity.Region);
        assert(0);
        return;
    }

    // Pending entries now have job dependency references. Completed entries can
    // be released. Later jobs wait for this writer, which waits for those earlier
    // dependencies before it can finish.
    __DropReference(job->Pools, job->PoolCount, &buffer->Writer);
    for (unsigned int i = 0; i < buffer->ReaderCapacity; i++) {
        __DropReference(job->Pools, job->PoolCount, &buffer->Readers[i]);
    }

    buffer->Writer = job->Completion;
    buffer->WriterResult = OS_EOK;
}

static void
__DropDependencies(
    _InOut_ OSBufferAccessJob_t* job)
{
    for (size_t i = 0; i < job->DependencyCount; i++) {
        __DropReference(job->Pools, job->PoolCount, &job->Scratch.Dependencies[i].Fence);
    }
}

oserr_t
OSBufferAccessTryPublish(
    _In_    const OSFencePool_t*           pools,
    _In_    unsigned int                   poolCount,
    _In_    const OSBufferAccessRequest_t* requests,
    _In_    size_t                         count,
    _InOut_ OSFenceReference_t*            completion,
    _InOut_ OSBufferAccessJob_t*           job)
{
    OSBufferAccessJob_t  prepared = { 0 };
    OSFenceReference_t   reference;
    const OSFencePool_t* pool;
    size_t               references = 0;
    oserr_t              status;

    if (pools == NULL || poolCount == 0) {
        WARNING("OSBufferAccessTryPublish: missing pool bindings");
        return OS_EINVALPARAMS;
    }

    if (requests == NULL || count == 0) {
        WARNING("OSBufferAccessTryPublish: missing buffer requests");
        return OS_EINVALPARAMS;
    }

    if (completion == NULL || job == NULL) {
        WARNING("OSBufferAccessTryPublish: missing completion or job");
        return OS_EINVALPARAMS;
    }

    if (job->Completion.Generation != 0) {
        TRACE("OSBufferAccessTryPublish: job %p is active", job);
        return OS_EBUSY;
    }

    if (count > job->Scratch.BufferCapacity || count > SIZE_MAX / sizeof(*requests)) {
        TRACE("OSBufferAccessTryPublish: %zu requests exceed scratch capacity %zu",
            count, job->Scratch.BufferCapacity);
        return OS_EBUFFER;
    }

    if (job->Scratch.Buffers == NULL ||
        (job->Scratch.DependencyCapacity != 0 && job->Scratch.Dependencies == NULL)) {
        WARNING("OSBufferAccessTryPublish: missing scratch arrays");
        return OS_EINVALPARAMS;
    }

    // Prepare a separate job so failure leaves the caller's record unchanged.
    // Only the scratch arrays are shared before success.
    prepared.Scratch = job->Scratch;
    prepared.Pools = pools;
    prepared.PoolCount = poolCount;
    prepared.Completion = *completion;
    pool = __ReferencePool(pools, poolCount, completion);

    // Validate the completion before accessing shared tables. Release this extra
    // reference on every exit, also on success.
    status = OSFencePoolAcquire(pool, completion);
    if (status != OS_EOK) {
        WARNING("OSBufferAccessTryPublish: completion pool %u slot %u generation %u acquire failed (%u)",
            completion->Pool, completion->Slot, completion->Generation, status);
        return status;
    }
    
    reference = *completion;
    status = __PrepareBuffers(requests, count, &prepared);
    if (status != OS_EOK) {
        goto exit;
    }
    
    status = __LockBuffers(&prepared);
    if (status != OS_EOK) {
        goto exit;
    }
    
    // Keep all tables locked while adding dependency references and one completion
    // reference per unique region. Table entries stay unchanged during this step.
    for (size_t i = 0; i < prepared.BufferCount; i++) {
        status = __CaptureBuffer(&prepared, &prepared.Scratch.Buffers[i]);
        if (status != OS_EOK) {
            goto rollback;
        }

        status = OSFencePoolAcquire(pool, completion);
        if (status != OS_EOK) {
            goto rollback;
        }
        references++;
    }
    
    // Submit is the last operation that can fail. References and table capacity
    // have been checked under the locks. Update all tables before unlocking so
    // another publisher cannot see a partial access list.
    status = OSFencePoolSubmit(pool, completion);
    if (status != OS_EOK) {
        goto rollback;
    }
    
    for (size_t i = 0; i < prepared.BufferCount; i++) {
        __PublishBuffer(&prepared, &prepared.Scratch.Buffers[i]);
    }
    __UnlockBuffers(&prepared, prepared.BufferCount);
    
    // Move the producer reference to the job and clear the input slot. The caller
    // must now use the job API to signal and release it.
    *job = prepared;
    memset(completion, 0, sizeof(OSFenceReference_t));
    
    TRACE("OSBufferAccessTryPublish: job %p published %zu buffers with %zu dependencies",
        job, job->BufferCount, job->DependencyCount);
    goto exit;

rollback:
    // Table entries are unchanged. Release dependency and unused table references
    // while the producer reference still keeps the completion valid.
    __DropDependencies(&prepared);
    while (references != 0) {
        references--;
        reference = *completion;
        __DropReference(pools, poolCount, &reference);
    }
    reference = *completion;
    __UnlockBuffers(&prepared, prepared.BufferCount);

exit:
    // Release the validation reference. On success, the job and table entries
    // now hold references to the completion.
    __DropReference(pools, poolCount, &reference);
    if (status != OS_EOK && status != OS_EBUSY && status != OS_EBUFFER) {
        WARNING("OSBufferAccessTryPublish: job %p publication failed (%u); temporary references released",
            job, status);
    }
    return status;
}

oserr_t
OSBufferAccessPoll(
    _InOut_ OSBufferAccessJob_t* job,
    _Out_   oserr_t*             resultOut)
{
    OSBufferDependency_t* dependency;
    size_t                i;
    bool                  pending = false;
    oserr_t               result;
    oserr_t               status;

    if (job == NULL || resultOut == NULL || job->Completion.Generation == 0) {
        TRACE("OSBufferAccessPoll: missing result output or unpublished job");
        return OS_EINVALPARAMS;
    }

    // Continue past pending entries so later completed references can be released.
    // Cleared entries stay in scratch and are skipped on the next poll.
    for (i = 0; i < job->DependencyCount; i++) {
        dependency = &job->Scratch.Dependencies[i];
        if (dependency->Fence.Generation == 0) {
            continue;
        }

        status = __PollReference(job->Pools, job->PoolCount, &dependency->Fence, &result);
        if (status == OS_EINPROGRESS) {
            pending = true;
            continue;
        } else if (status != OS_EOK) {
            TRACE("OSBufferAccessPoll: dependency %zu, pool %u slot %u generation %u poll failed (%u)",
                i, dependency->Fence.Pool, dependency->Fence.Slot, dependency->Fence.Generation, status);
            return status;
        }

        if (dependency->NeedsContents && job->DependencyResult == OS_EOK) {
            job->DependencyResult = result;
        }
        __DropReference(job->Pools, job->PoolCount, &dependency->Fence);
    }

    // All earlier accesses must end before this job can complete, also when a
    // required writer failed. Later jobs may already be waiting on this one.
    if (pending) {
        return OS_EINPROGRESS;
    }

    *resultOut = job->DependencyResult;
    return OS_EOK;
}

oserr_t
OSBufferAccessFinish(
    _InOut_ OSBufferAccessJob_t* job,
    _In_    oserr_t              result)
{
    OSBufferAccessScratch_t scratch;
    const OSFencePool_t*    pool;
    oserr_t                 dependencyResult;
    oserr_t                 status;

    // Dependencies must finish before this job, also on cancellation. Poll may
    // release completed dependencies but does not signal the job.
    status = OSBufferAccessPoll(job, &dependencyResult);
    if (status != OS_EOK) {
        return status;
    }

    // Use the job error if one was supplied. Otherwise pass on a required writer's
    // error so later jobs can see that the contents are invalid.
    if (result == OS_EOK) {
        result = dependencyResult;
    }

    pool = __ReferencePool(job->Pools, job->PoolCount, &job->Completion);
    status = OSFencePoolSignal(pool, &job->Completion, result);
    if (status != OS_EOK) {
        // Consumers may already see completion. Keep the job and producer
        // reference so notification can be retried with the same result.
        ERROR("OSBufferAccessFinish: pool %u slot %u generation %u signal failed (%u, result %u); keeping job for retry",
            job->Completion.Pool, job->Completion.Slot, job->Completion.Generation, status, result);
        return status;
    }

    // Table references stay until replaced or reclaimed. Release the local job
    // reference and keep its scratch arrays for reuse.
    __DropReference(job->Pools, job->PoolCount, &job->Completion);
    scratch = job->Scratch;
    memset(job, 0, sizeof(OSBufferAccessJob_t));
    job->Scratch = scratch;
    return OS_EOK;
}

oserr_t
OSBufferReservationReclaim(
    _In_ const OSFencePool_t*   pools,
    _In_ unsigned int           poolCount,
    _In_ OSBufferReservation_t* buffer)
{
    unsigned int expected = 0;
    oserr_t      status;

    if (pools == NULL || buffer == NULL || buffer->Storage == NULL) {
        WARNING("OSBufferReservationReclaim: missing pool or buffer binding");
        return OS_EINVALPARAMS;
    }

    if (!atomic_compare_exchange_strong_explicit(&buffer->Storage->Lock,
        &expected, 1, memory_order_acquire, memory_order_relaxed)) {
        return OS_EBUSY;
    }
    
    status = __ReclaimBuffer(pools, poolCount, buffer->Storage);
    atomic_store_explicit(&buffer->Storage->Lock, 0, memory_order_release);
    if (status != OS_EOK) {
        WARNING("OSBufferReservationReclaim: buffer %u region %u reclaim failed (%u)",
            buffer->Identity.Buffer, buffer->Identity.Region, status);
    }
    return status;
}
