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
struct SHMRecord;

/**
 * @brief CPU or DMA operations for a HandleTypeSHMAccess payload.
 * Release ends explicit use. On failure, the object stays available for retry.
 * Abandon runs after the final grant reference is dropped or all group threads exit.
 * It must cancel queued access and release or quarantine the object.
 * Both cleanup operations receive a detached slot and clear it on success.
 * Begin and End may be NULL when unsupported. Ops must outlive the record.
 */
struct SHMRecordOps {
    oserr_t (*Release)(struct SHMRecord*, void**);
    oserr_t (*Abandon)(struct SHMRecord*, void**);
    oserr_t (*Begin)(struct SHMRecord*, const struct SHMAccessBeginParameters*, OSAsyncContext_t*);
    oserr_t (*End)(struct SHMRecord*, const struct SHMAccessEndParameters*);
};

enum SHMRecordState {
    SHMRecordIdle,
    // Covers setup, copy-out and an entire operation, including an async child.
    SHMRecordUpdating,
    SHMRecordClosed
};

/**
 * @brief CPU session, DMA context or DMA mapping stored in a kernel handle.
 * The handle grant controls access and lifetime. State rejects concurrent calls
 * with OS_EBUSY instead of waiting for another operation to finish.
 */
struct SHMRecord {
    uuid_t                       Handle;
    _Atomic(enum SHMRecordState)  State;
    const struct SHMRecordOps*    Ops;
    void*                        Object;
};

/**
 * @brief Create an empty record in Updating state with its initial handle reference.
 * Set Object during setup. Store Idle only after the result is copied out successfully.
 * On failure, release any object and call SHMRecordRemove.
 * @param ops Static resource operations.
 * @param recordOut Receives the record on success.
 * @return OS_EOK or OS_EOOM.
 */
oserr_t
SHMRecordCreate(
    _In_  const struct SHMRecordOps* ops,
    _Out_ struct SHMRecord**        recordOut);

/**
 * @brief Check the caller's grant, add an operation reference and enter Updating.
 * @param handle Kernel access handle.
 * @param ops Required operations, or NULL to accept any access resource.
 * @param recordOut Receives the record until End or successful release.
 * @return OS_EOK, OS_ENOENT, OS_EINVALPARAMS or OS_EBUSY.
 */
oserr_t
SHMRecordBegin(
    _In_  uuid_t                     handle,
    _In_  const struct SHMRecordOps* ops,
    _Out_ struct SHMRecord**        recordOut);

/**
 * @brief Return to Idle and drop the operation reference in the same context.
 * A forked parent leaves this to its child. Do not call after successful removal.
 * @param record Record returned by SHMRecordBegin.
 */
void
SHMRecordEnd(
    _In_ struct SHMRecord* record);

/**
 * @brief Release the object. The record must be in Updating state.
 * On failure, the object stays available for retry.
 * @param record Record during setup or an operation.
 * @return OS_EOK or the resource's release error.
 */
oserr_t
SHMRecordRelease(
    _In_ struct SHMRecord* record);

/**
 * @brief Mark Closed and drop the initial reference after setup failure or release.
 * Any operation reference must be dropped separately with DestroyHandle.
 * @param record Record in Updating state. Do not use after this call without a reference.
 */
void
SHMRecordRemove(
    _In_ struct SHMRecord* record);

#endif
