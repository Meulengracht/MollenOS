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
 * Tracks who is reading or writing a range of shared memory so CPU access
 * and DMA transfers do not overwrite each other or clash on cache lines.
 * Because CPU caches work on whole cache lines at a time, adjacent byte ranges
 * that fall in the same cache line are treated as overlapping.
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
 * @brief Wait settings for requesting access to a memory range.
 *
 * When Blocking is true, SHMAccessBegin waits until the range is free (matching
 * SHMWaitUntilAvailable) or Deadline expires. When Blocking is false, it returns
 * OS_EBUSY right away if someone else holds an overlapping range. If Async is
 * provided, a blocking call can return OS_EFORKED and finish waiting on a
 * kernel worker thread. Both the wait flags and Deadline are copied before
 * waiting begins.
 */
struct SHMAccessWait {
    bool                 Blocking;
    OSAsyncContext_t*    Async;
    const OSTimestamp_t* Deadline;
};

enum SHMAccessPhase {
    SHMAccessIdle,
    SHMAccessPending,
    SHMAccessWaiting,
    SHMAccessActive,
    SHMAccessQuarantined
};

// Who currently holds access to the memory range.
enum SHMAccessHolder {
    SHMAccessHolderNone,
    SHMAccessHolderCpu,
    SHMAccessHolderDevice
};

/**
 * @brief A reusable reservation for a byte range inside a shared memory buffer.
 *
 * Buffer->Mutex guards the buffer's access list along with Phase, Holder, and
 * Cancelled. Once Cancelled is set, it stays set for the lifetime of this
 * struct so it can never be reused.
 *
 * Idle reservations have no holder. SHMAccessBegin sets Holder to the requester
 * before checking for conflicts. SHMAccessQuarantine sets Holder to
 * SHMAccessHolderDevice and locks the whole fixed range until hardware stops,
 * preventing any CPU or device from using memory that a lost device might still
 * touch.
 *
 * Offset and Length record the fixed range given at initialization. First and
 * Last hold the current request's bounds rounded outward to whole cache lines
 * (a quarantine locks the entire fixed range). BlockedBy points to the last
 * conflicting reservation ahead of this one in the queue, so waiting callers
 * wake up in order as each reservation ahead of them finishes.
 *
 * Event is a sleep/wake counter used with FutexWait and FutexWake. Keep this
 * struct allocated until SHMAccessBegin returns, including when a wait finishes
 * on a worker thread after returning OS_EFORKED.
 */
typedef struct SHMAccess {
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
    bool                 Cancelled;
    enum SHMAccessPhase  Phase;
    enum SHMAccessHolder Holder;
    // Saved copy of the deadline so a worker thread can read it after the caller returns.
    OSTimestamp_t        Deadline;
    atomic_int           Event;
} SHMAccess_t;

/**
 * @brief Sets up an idle reservation for a verified, nonempty SHM byte range.
 *
 * @param access Reservation storage to initialize.
 * @param buffer Backing SHM buffer.
 * @param offset First byte relative to the buffer.
 * @param length Nonzero byte count for the fixed range.
 */
KERNELABI void KERNELAPI
SHMAccessInitialize(
    _Out_ struct SHMAccess* access,
    _In_  struct SHMBuffer* buffer,
    _In_  size_t            offset,
    _In_  size_t            length);

/**
 * @brief Creates a CPU range reservation and keeps the SHM buffer's pages allocated.
 *
 * The internal buffer reference stays valid even if the creating process exits
 * before the reservation is destroyed.
 *
 * @param shm       Handle of an ordinary (non-region) SHM buffer.
 * @param offset    First byte relative to the buffer.
 * @param length    Nonzero byte count for the range.
 * @param accessOut Receives the new idle reservation on success.
 * @return OS_EOK, or an error if the handle is invalid, the range does not fit,
 *         or memory runs out.
 */
KERNELABI oserr_t KERNELAPI
SHMAccessCreate(
    _In_  uuid_t             shm,
    _In_  size_t             offset,
    _In_  size_t             length,
    _Out_ struct SHMAccess** accessOut);

/**
 * @brief Requests access to a byte range for the CPU or a device.
 *
 * Conflicting queued requests are served in arrival order. When blocking is
 * enabled, returning OS_EFORKED means a kernel worker thread took over the wait
 * and will finish it in the background. When the range is free, this succeeds
 * right away without allocating memory.
 *
 * @param access Initialized reservation. Callers must make sure calls on the
 *               same reservation do not overlap.
 * @param offset First byte relative to this reservation's fixed range.
 * @param length Nonzero byte count. Must fit inside the fixed range.
 * @param holder SHMAccessHolderCpu or SHMAccessHolderDevice.
 * @param write  True for exclusive write access, false for shared read access.
 * @param wait   Wait settings, or NULL to fail immediately if busy.
 * @return OS_EOK, OS_EFORKED, OS_EBUSY if busy (non-blocking), OS_EDEVFAULT if
 *         the range is locked down after a device loss, OS_ECANCELLED if
 *         cancelled, or a wait error.
 */
KERNELABI oserr_t KERNELAPI
SHMAccessBegin(
    _In_ struct SHMAccess*           access,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ enum SHMAccessHolder        holder,
    _In_ bool                        write,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief Checks if an active persistent mapping can start another device job.
 *
 * Returns OS_EBUSY when another conflicting request is waiting in line, letting
 * current device jobs drain so the waiting caller can take its turn.
 *
 * @param access Active reservation belonging to a persistent mapping.
 * @return OS_EOK, OS_EBUSY if a conflicting waiter is queued, OS_EDEVFAULT if
 *         an overlapping range is locked down after a device loss, or OS_ECANCELLED.
 */
KERNELABI oserr_t KERNELAPI
SHMAccessExtend(
    _In_ struct SHMAccess* access);

/**
 * @brief Releases an active or queued reservation.
 *
 * Idle and locked-down (quarantined) reservations are left alone, since only
 * SHMAccessCompleteStop clears a lockdown after hardware stops. Before calling
 * this, finish all device transfers, cache operations, and memory barriers. CPU
 * callers must currently hold the reservation in the active state.
 *
 * @param access Reservation with no remaining CPU or device work running.
 */
KERNELABI void KERNELAPI
SHMAccessEnd(
    _In_ struct SHMAccess* access);

/**
 * @brief Locks the full fixed range against new access until hardware stops.
 *
 * Cancels any waiting request on this reservation, including a wait running on
 * a worker thread (which wakes up, returns OS_ECANCELLED, and leaves the full
 * range blocked). Keep the containing struct valid until that waiter exits.
 *
 * @param access Reservation for a range the device might still touch.
 */
KERNELABI void KERNELAPI
SHMAccessQuarantine(
    _In_ struct SHMAccess* access);

/**
 * @brief Permanently cancels this reservation and wakes any waiting caller.
 *
 * Does not end a transfer that is already running, clear a lockdown, or free
 * the struct. Keep the struct allocated until all callers inside SHMAccessBegin
 * return.
 *
 * @param access Initialized reservation in any phase.
 */
KERNELABI void KERNELAPI
SHMAccessCancel(
    _In_ SHMAccess_t* access);

/**
 * @brief Reads the reservation's current phase under Buffer->Mutex.
 *
 * Because the lock is dropped before returning, the result is only a point-in-time
 * check. Callers must keep the reservation and buffer alive and avoid overlapping
 * cleanup calls on their own.
 *
 * @param access Initialized reservation to check.
 * @return Current SHMAccessPhase value.
 */
KERNELABI enum SHMAccessPhase KERNELAPI
SHMAccessGetPhase(
    _In_ SHMAccess_t* access);

/**
 * @brief Clears a reservation after hardware has stopped and caches are synced.
 *
 * Call this only after all waiters have exited, cache cleanup has finished, and
 * you know for sure the device cannot touch this range anymore. Keeps Cancelled
 * set so the reservation cannot be reused. Does nothing if already idle.
 *
 * @param access Reservation to remove from the buffer's active list.
 */
KERNELABI void KERNELAPI
SHMAccessCompleteStop(
    _In_ SHMAccess_t* access);

/**
 * @brief Frees an idle CPU reservation created by SHMAccessCreate.
 *
 * Safe to call from another process context when the creating process exits.
 *
 * @param access Idle reservation pointer, set to NULL on success.
 * @return OS_EOK, or OS_EBUSY if the reservation is still queued or active.
 */
KERNELABI oserr_t KERNELAPI
SHMAccessDestroy(
    _InOut_ struct SHMAccess** access);

#endif //!__SHM_ACCESS_H__
