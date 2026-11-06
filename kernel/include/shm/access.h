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

/**
 * Shared range reservations for CPU access and DMA cache handoffs.
 */
#ifndef __SHM_ACCESS_H__
#define __SHM_ACCESS_H__

#include <os/osdefs.h>
#include <os/types/async.h>
#include <os/types/time.h>
#include <os/types/grants.h>
#include <stdatomic.h>

struct SHMBuffer;

/**
 * Wait options for range admission. When Blocking is true, use
 * SHMWaitUntilAvailable as defined by SHMWaitMode. Otherwise, use
 * SHMWaitNonBlocking.
 * Async allows the syscall to fork while waiting. The options and absolute
 * Deadline are copied before any wait.
 */
struct SHMAccessWait {
    bool                 Blocking;
    OSAsyncContext_t*    Async;
    const OSTimestamp_t* Deadline;
};

/** Reservation phases. Reservations are linked into the buffer list unless idle. */
enum SHMAccessPhase {
    SHMAccessIdle,
    // Admission is checking the list while holding the buffer mutex.
    SHMAccessPending,
    // A conflicting request must finish before admission can continue.
    SHMAccessWaiting,
    SHMAccessActive,
    // Hardware may still use the range. Conflicting requests fail.
    SHMAccessQuarantined
};

/** Requested user while queued, or current user while active or quarantined. */
enum SHMAccessHolder {
    SHMAccessHolderNone,
    SHMAccessHolderCpu,
    SHMAccessHolderDevice
};

/**
 * A reusable reservation. Buffer->Mutex protects the list, Phase and Holder.
 * Idle reservations have no holder. SHMAccessBegin sets the requested holder
 * before admission. SHMAccessQuarantine sets Holder to SHMAccessHolderDevice
 * until the device has confirmed a stop.
 * Offset and Length identify the fixed range within Buffer. First and Last
 * describe the cache-line bounds selected by SHMAccessBegin while the reservation
 * is queued or active. Quarantining an idle reservation selects its entire fixed
 * range.
 * BlockedBy identifies the last conflicting reservation found during admission.
 * When that reservation is removed, this reservation is rechecked before either
 * record can be freed.
 * Event is a futex sequence for queued callers. The containing object must
 * remain allocated until SHMAccessBegin finishes, including when it runs in an
 * async child.
 */
struct SHMAccess {
    struct SHMAccess*    Next;
    struct SHMAccess*    BlockedBy;
    struct SHMBuffer*    Buffer;
    uuid_t               ReferenceID;
    GrantTokenScope_t    Scope;
    size_t               Offset;
    size_t               Length;
    size_t               First;
    size_t               Last;
    bool                 Write;
    enum SHMAccessPhase  Phase;
    enum SHMAccessHolder Holder;
    // Stable wait state for an async child after the caller returns.
    OSTimestamp_t        Deadline;
    atomic_int           Event;
};

/**
 * @brief Initialize an idle reservation for a validated, nonempty SHM range.
 *
 * @param access Reservation storage to initialize.
 * @param buffer Buffer containing the validated range.
 * @param offset First byte relative to the buffer.
 * @param length Nonzero number of bytes in the range.
 */
void
SHMAccessInitialize(
    _Out_ struct SHMAccess* access,
    _In_  struct SHMBuffer* buffer,
    _In_  size_t            offset,
    _In_  size_t            length);

/**
 * @brief Create a CPU reservation and keep the SHM pages allocated.
 * The backing handle reference is independent of the creating context.
 *
 * @param shm Handle of an ordinary SHM buffer.
 * @param offset First byte relative to the buffer.
 * @param length Nonzero number of bytes in the range.
 * @param accessOut Receives the new idle reservation.
 * @return OS_EOK, or a handle, range-validation or allocation error.
 */
oserr_t
SHMAccessCreate(
    _In_  uuid_t             shm,
    _In_  size_t             offset,
    _In_  size_t             length,
    _Out_ struct SHMAccess** accessOut);

/**
 * @brief Reserve a range. Earlier conflicting queued requests are handled first.
 * Wait behavior is defined by SHMWaitMode. OS_EFORKED leaves the reservation
 * with the async child, which finishes this call. No allocation occurs on the
 * uncontended path.
 *
 * @param access Initialized reservation. The caller must serialize calls on it.
 * @param offset First byte relative to the reservation's fixed range.
 * @param length Nonzero number of bytes, validated to fit within the fixed range.
 * @param holder Must be SHMAccessHolderCpu or SHMAccessHolderDevice.
 * @param write True for exclusive write access. False for shared reads.
 * @param wait Wait options, or NULL for nonblocking access.
 * @return OS_EOK, OS_EBUSY, OS_EDEVFAULT, or the wait result.
 */
oserr_t
SHMAccessBegin(
    _In_ struct SHMAccess*           access,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ enum SHMAccessHolder        holder,
    _In_ bool                        write,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief Check admission before extending an active persistent use batch.
 * Return OS_EBUSY if a conflicting request is pending so existing uses can finish.
 *
 * @param access Reservation for an active persistent use batch.
 * @return OS_EOK, OS_EBUSY, or OS_EDEVFAULT.
 */
oserr_t
SHMAccessExtend(
    _In_ struct SHMAccess* access);

/**
 * @brief Release a queued or active reservation. Idle reservations are unchanged.
 * No SHMAccessBegin call may resume. The caller must finish all device work, cache
 * maintenance and memory barriers before calling. CPU callers require Phase to
 * be SHMAccessActive and Holder to be SHMAccessHolderCpu.
 *
 * @param access Serialized reservation with no remaining CPU or device work.
 */
void
SHMAccessEnd(
    _In_ struct SHMAccess* access);

/**
 * @brief Block a range until the device has confirmed a stop.
 * No suspended SHMAccessBegin call may resume after this transition.
 * Idle reservations block their entire fixed range. Queued or active
 * reservations keep the range selected by SHMAccessBegin.
 *
 * @param access Reservation for a range the device may still use.
 */
void
SHMAccessQuarantine(
    _In_ struct SHMAccess* access);

/**
 * @brief Destroy an idle CPU reservation created by SHMAccessCreate.
 * May run in another context after the creating group exits.
 *
 * @param access Idle CPU reservation. Cleared on success. The caller must serialize calls.
 * @return OS_EOK, or OS_EBUSY if access is still queued or active.
 */
oserr_t
SHMAccessDestroy(
    _InOut_ struct SHMAccess** access);

#endif
