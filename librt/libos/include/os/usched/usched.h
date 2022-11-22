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
 * @brief Yields control of the current task and executes the next task in line. If no tasks
 * are ready to execute, control is returned to original caller of this function.
 * @param[In] deadline If there were no more jobs to execute, but there will be in the
 *                     future, for instance that are either sleeping or waiting for a timeout,
 *                     then this will be set to the point in time when usched_yield should be called
 *                     again. This parameter is optional, and does not need to be provided if a job
 *                     simply wants to give up it's timeslice voluntarily.
 * @return Returns the next action available for the scheduler.
 *         A value of 0 means that there are jobs ready to execute now.
 *         A value of -1 means that the caller needs to block, as either of two cases have occurred.
 *         ENOENT means that there are no more jobs to execute, and no events pending.
 *         EWOULDBLOCK means that there are no jobs to execute, but we have an event pending and
 *         usched_yield should be called again once the time specified by deadline has been reached.
 */
CRTDECL(int, usched_yield(struct timespec* deadline));

/**
 * @brief Puts the calling thread/execution unit to sleep until a new job has been queued.
 */
CRTDECL(void, usched_wait(void));

/**
 * @brief Puts the calling thread/execution unit to sleep until a new job or the point in time
 * specified by 'until' has been reached.
 * @param[In] until A point in time for which the execution unit must be woken up by.
 */
CRTDECL(void, usched_timedwait(const struct timespec* until));

/**
 * @brief Blocks the current job until the async context has completed its
 * action.
 * @param asyncContext The async context associated with the async action.
 */
CRTDECL(void, usched_wait_async(OSAsyncContext_t* asyncContext));

/**
 * @brief Retrieves the notification queue handle for the current scheduler.
 * This is used by the system call subsystem to notify the scheduler of new syscall
 * completion events.
 * @return The handle of the scheduler notification queue.
 */
CRTDECL(uuid_t, usched_notification_handle(void));

#endif //!__OS_USCHED_H__
