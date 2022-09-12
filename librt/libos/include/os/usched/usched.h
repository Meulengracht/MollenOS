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

#include <os/usched/types.h>
#include <stdbool.h>

struct usched_job_parameters {
    // The stack size for the job. The default stack-size will be 16KB
    unsigned int stack_size;

    // Detached controls whether this job runs as a part of the common
    // worker pool. A detached job is assigned its own, seperate execution unit. This
    // execution unit runs only this job, and will sleep when the job is not ready.
    bool detached;

    // Setting an affinity mask for a job can result in jobs receiving less processor time,
    // as the system is restricted from running the jobs on certain execution units. In most cases,
    // it is better to let usched select an execution unit. The affinity mask must point to a array
    // big enough to encompass enough bits for largest cpu id. For instance, on a system with 64
    // execution units, this should point to 64 bit of storage (2 DWORDs).
    unsigned int* affinity_mask;
};

/**
 * @brief Initialize the job parameters to the default values.
 * @param params
 */
CRTDECL(void, usched_job_parameters_init(struct usched_job_parameters* params));

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
 * @brief Schedules a new task in the scheduler for current execution unit.
 *
 * @param entry    The function to execute with the usched_task_fn signature.
 * @param argument The argument that should be passed to the function.
 * @return         A cancellation token value that can be used to signal cancellation to the task.
 *                 The cancellation token is passed to the task entry as the second parameter.
 */
CRTDECL(void*, usched_task_queue(usched_task_fn entry, void* argument));

/**
 * @brief Schedules a new task in the scheduler for current execution unit.
 *
 * @param entry    The function to execute with the usched_task_fn signature.
 * @param argument The argument that should be passed to the function.
 * @param params   Configuration parameters for the job
 * @return         A cancellation token value that can be used to signal cancellation to the task.
 *                 The cancellation token is passed to the task entry as the second parameter.
 */
CRTDECL(void*, usched_task_queue3(usched_task_fn entry, void* argument, struct usched_job_parameters* params));

/**
 * @brief Marks the currently running task as cancelled. The cancelation token will be
 * signaled.
 */
CRTDECL(void, usched_task_cancel_current(void));

/**
 * @brief Signals to the task's cancellation token that the a cancel operation has been requested.
 *
 * @param cancellationToken The cancellation token that should be signalled.
 */
CRTDECL(void, usched_task_cancel(void* cancellationToken));

/**
 * @brief For tasks to implement check of whether or not a cancellation token has been signalled.
 *
 * @param cancellationToken The token to check.
 * @return                  Returns 1 if the token is signaled. Returns 0 if not.
 */
CRTDECL(int, usched_ct_is_cancelled(void* cancellationToken));

#endif //!__OS_USCHED_H__
