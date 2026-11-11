/**
 * Copyright 2021, Philip Meulengracht
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
 * User threads implementation. Implements support for multiple tasks running entirely
 * in userspace. This is supported by additional synchronization primitives in the usched_
 * namespace.
 *
 */

#ifndef __OS_USCHED_H__
#define __OS_USCHED_H__

// imported from time.h
struct timespec;

// imported from async.h
typedef struct OSAsyncContext OSAsyncContext_t;

/**
 * @brief Yields the current job to the execution unit's scheduler stack.
 * The job may resume on another unit. Called outside a job, this runs jobs
 * until the unit is idle or asked to stop.
 *
 * @param deadline Optional output for the next timer deadline (TIME_UTC).
 *                 Set when no jobs are ready and a timer is pending.
 * @return 0 when a job resumes. Outside a job, 0 means a timer is due and
 *         this function should be called again.
 *         -1 with errno set to ENOENT if no jobs are ready and no timers remain,
 *         EWOULDBLOCK if the next timer is still pending, or ESHUTDOWN if the
 *         execution unit has been asked to stop.
 */
CRTDECL(int, usched_yield(struct timespec* deadline));

/**
 * @brief Puts the calling execution unit to sleep until its scheduler is notified.
 */
CRTDECL(void, usched_wait(void));

/**
 * @brief Puts the calling execution unit to sleep until its scheduler is notified
 * or the deadline is reached.
 *
 * @param until Absolute deadline (TIME_UTC). NULL means no timeout.
 */
CRTDECL(void, usched_timedwait(const struct timespec* until));

/**
 * @brief Blocks the current job until its async syscall completes.
 */
CRTDECL(void, usched_wait_async(void));

/**
 * @brief Retrieves the current scheduler's notification handle for syscall
 * completions. All execution units on the scheduler share this handle, so it
 * stays valid when a job moves to another unit or its previous unit stops.
 *
 * @return The ID of the scheduler notification handle.
 */
CRTDECL(uuid_t, usched_notification_handle(void));

#endif //!__OS_USCHED_H__
