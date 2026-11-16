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
 * Release runs when user code explicitly closes the resource. Returning an error
 * leaves the object alive so the caller can try closing it again.
 * Cleanup runs when the last handle reference goes away or when every thread
 * in the process group exits. It must either free the object or lock down its
 * memory range until hardware stops.
 *
 * Both Release and Cleanup receive a pointer to the object slot (void**) and
 * must set that slot to NULL once cleanup finishes. Begin and End can be NULL
 * if the resource does not support range access. Keep the Ops table valid in
 * memory for as long as the record lives.
 *
 * CancelOperation is optional. It runs during process shutdown when a thread
 * was interrupted inside a blocked system call (such as waiting for a busy
 * memory range), and runs after all threads in the group have stopped even if
 * Object is NULL. Returning an error leaves PendingOperation set and keeps the
 * record alive, which prevents Cleanup from running on memory that might still
 * be in use.
 */
typedef struct SHMRecordOps {
    oserr_t (*Release)(SHMRecord_t*, void**);
    oserr_t (*Cleanup)(SHMRecord_t*, void**);
    oserr_t (*Begin)(SHMRecord_t*, const struct SHMAccessBeginParameters*, OSAsyncContext_t*);
    oserr_t (*End)(SHMRecord_t*, const struct SHMAccessEndParameters*);
    oserr_t (*CancelOperation)(SHMRecord_t*);
} SHMRecordOps_t;

enum SHMRecordState {
    SHMRecordIdle,
    SHMRecordUpdating,
    SHMRecordClosed
};

/**
 * @brief Wraps a CPU access session, DMA context, or DMA mapping in a kernel handle.
 *
 * The kernel handle table controls which process can look up this record and
 * when it gets freed. State makes sure only one system call touches the record
 * at a time, returning OS_EBUSY right away when another call is already running
 * on it.
 */
typedef struct SHMRecord {
    uuid_t                       Handle;
    _Atomic(enum SHMRecordState) State;
    const SHMRecordOps_t*        Ops;
    void*                        Object;
    void*                        PendingOperation;
} SHMRecord_t;

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
