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
 * Handle wrapper for shared-memory CPU sessions, DMA contexts, DMA mappings,
 * and DMA region sessions. Serializes system calls per handle and drives
 * cleanup when a handle closes or its process exits.
 */

#ifndef __SHM_RECORD_H__
#define __SHM_RECORD_H__

#include <os/osdefs.h>
#include <os/types/async.h>
#include <stdatomic.h>

struct SHMAccessBeginParameters;
struct SHMAccessEndParameters;
DECL_STRUCT(SHMRecord);

/**
 * @brief Callbacks for a kernel handle of type HandleTypeSHMAccess.
 *
 * Release runs when user code explicitly closes the handle. Returning an error
 * leaves the object alive so the caller can try closing it again.
 * Cleanup runs when setup or handle publication fails, when the last handle
 * reference drops, or when every thread in the process group exits. It must
 * either free the object or lock its memory range until the hardware stops.
 *
 * Both Release and Cleanup own record->Object while they run. They must set it
 * to NULL once the object is gone, and leave it set when the object survives so
 * a later retry can find it. Most resources use SHMRecordDetachObject before
 * destroying the object, so an interrupted thread never leaves record->Object
 * pointing at freed memory, and SHMRecordAttachObject afterwards to keep
 * whatever is left. A resource whose teardown can be interrupted and retried
 * leaves record->Object set until the object is unlinked, and clears it itself.
 *
 * Begin and End can be NULL if the resource does not support range access.
 * Keep Ops valid in memory for as long as the record lives.
 *
 * CancelOperation is optional. It runs during process shutdown when a thread
 * was interrupted inside a blocked system call (such as waiting for a busy
 * memory range). It runs after all threads in the process group have stopped,
 * even if Object is NULL. Returning an error leaves PendingOperation set and
 * keeps the record alive, so Cleanup never frees memory that might still be in
 * use by hardware or a stalled wait.
 */
typedef struct SHMRecordOps {
    oserr_t (*Release)(SHMRecord_t*);
    oserr_t (*Cleanup)(SHMRecord_t*);
    oserr_t (*Begin)(SHMRecord_t*, const struct SHMAccessBeginParameters*, OSAsyncContext_t*);
    oserr_t (*End)(SHMRecord_t*, const struct SHMAccessEndParameters*);
    oserr_t (*CancelOperation)(SHMRecord_t*);
} SHMRecordOps_t;

/**
 * @brief Serializes system calls on one SHMRecord.
 *
 * Only one caller moves a record from Idle to Updating at a time, so concurrent
 * syscalls on the same handle fail fast with OS_EBUSY instead of racing.
 */
enum SHMRecordState {
    // No system call or teardown holds the record right now.
    SHMRecordIdle,
    // Setup, teardown, or an active system call holds the record exclusively.
    SHMRecordUpdating,
    // The handle has been removed and can no longer be looked up.
    SHMRecordClosed
};

/**
 * @brief Wraps a CPU access session, DMA context, or DMA mapping in a kernel handle.
 *
 * The kernel handle table controls which process can look up this record and
 * when it gets freed. State makes sure only one system call touches the record
 * at a time, returning OS_EBUSY right away when another call is already running
 * on it. PendingOperation points to any mid-flight wait or region call so
 * process-exit cleanup can cancel or finish it before running Cleanup.
 */
typedef struct SHMRecord {
    uuid_t                       Handle;
    _Atomic(enum SHMRecordState) State;
    const SHMRecordOps_t*        Ops;
    void*                        Object;
    void*                        PendingOperation;
} SHMRecord_t;

/**
 * @brief Takes the object out of a record before it is destroyed.
 *
 * Clears record->Object so the record never points at an object that is being
 * freed. Pair this with SHMRecordAttachObject once the destroy call returns.
 *
 * @param record Record in the Updating state.
 * @return The object that was stored in the record, or NULL if it had none.
 */
static inline void*
SHMRecordDetachObject(
    _In_ SHMRecord_t* record)
{
    void* object = record->Object;
    record->Object = NULL;
    return object;
}

/**
 * @brief Puts back whatever is left of an object after a destroy attempt.
 *
 * @param record Record in the Updating state.
 * @param object The object pointer left by the destroy call. NULL if the
 *               object was destroyed.
 */
static inline void
SHMRecordAttachObject(
    _In_ SHMRecord_t* record,
    _In_ void*        object)
{
    record->Object = object;
}

/**
 * @brief Creates a new record in the Updating state with its initial handle reference.
 *
 * Store your object in Object during setup, and move State to SHMRecordIdle
 * only after copying the handle ID back to user space succeeds. If setup or
 * copying fails, clean up the object and call SHMRecordRemove.
 *
 * @param ops       Static callbacks for this resource type.
 * @param recordOut Receives the new record on success.
 * @return OS_EOK on success, or OS_EOOM if allocation fails.
 */
oserr_t
SHMRecordCreate(
    _In_  const SHMRecordOps_t* ops,
    _Out_ SHMRecord_t**         recordOut);

/**
 * @brief Looks up a handle, takes a reference for this call, and marks it Updating.
 *
 * @param handle    Kernel handle of type HandleTypeSHMAccess.
 * @param ops       Expected callback table, or NULL to accept any SHM access resource.
 * @param recordOut Receives the record pointer until SHMRecordEnd or release.
 * @return OS_EOK, OS_ENOENT if closed, OS_EINVALPARAMS if invalid or wrong type,
 *         or OS_EBUSY if another call is already using this record.
 */
oserr_t
SHMRecordBegin(
    _In_  uuid_t                handle,
    _In_  const SHMRecordOps_t* ops,
    _Out_ SHMRecord_t**         recordOut);

/**
 * @brief Returns a record to Idle and drops the reference from SHMRecordBegin.
 *
 * When a blocking wait hands off to a kernel worker thread (returning OS_EFORKED),
 * that worker thread calls this once it finishes. Never call this after
 * SHMRecordRemove has removed the handle.
 *
 * @param record Record returned by SHMRecordBegin.
 */
void
SHMRecordEnd(
    _In_ SHMRecord_t* record);

/**
 * @brief Runs the resource's Release callback while the record is Updating.
 *
 * If Release returns an error, the object stays attached so the caller can retry.
 *
 * @param record Record currently in the Updating state.
 * @return OS_EOK or the error returned by the resource's Release callback.
 */
oserr_t
SHMRecordRelease(
    _In_ SHMRecord_t* record);

/**
 * @brief Runs Cleanup right away when setting up or publishing a resource fails.
 *
 * The record must be in the Updating state and have no pending operation of
 * its own. Cleanup is given the object slot with the object taken out of the
 * record. If Cleanup fails, the object that is left is put back so that
 * handle teardown can try again later. If the slot is empty there is nothing to
 * clean up and no callback runs.
 *
 * This does not cancel operations, close the record, or end a parent
 * operation (for example, a submit operation keeping a parent mapping alive
 * while a child session is torn down). Keep any parent operation open while
 * Cleanup runs, then remove the failed record with SHMRecordRemove.
 *
 * @param record Record that is in the Updating state.
 * @return OS_EOK if the slot was empty or Cleanup succeeded. Otherwise the
 *         error from the Cleanup callback.
 */
KERNELABI oserr_t KERNELAPI
SHMRecordCleanup(
    _In_ SHMRecord_t* record);

/**
 * @brief Marks a record Closed and drops its initial handle reference.
 *
 * Call this after a failed setup or a successful SHMRecordRelease. If you also
 * entered through SHMRecordBegin, drop that separate reference with DestroyHandle.
 *
 * @param record Record in the Updating state. Do not read or write it afterward
 *               unless you still hold another handle reference.
 */
void
SHMRecordRemove(
    _In_ SHMRecord_t* record);

#endif //!__SHM_RECORD_H__
