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
 *
 * User Scheduler Definitions & Structures
 * - This header describes the base sched structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_USCHED_JOB_H__
#define __OS_USCHED_JOB_H__

// imported from time.h
struct timespec;

#include <os/osdefs.h>
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

_CODE_BEGIN

/**
 * @brief Initialize the job parameters to the default values.
 * @param params
 */
CRTDECL(void, usched_job_parameters_init(struct usched_job_parameters* params));

/**
 * @brief Sets the detached status of the job parameters.
 * @param params   The job parameters to update.
 * @param detached Whether the job should run detached or not.
 */
CRTDECL(void, usched_job_parameters_set_detached(struct usched_job_parameters* params, bool detached));

/**
 * @brief Schedules a new task in the scheduler for current execution unit.
 *
 * @param entry    The function to execute with the usched_task_fn signature.
 * @param argument The argument that should be passed to the function.
 * @return         A cancellation token value that can be used to signal cancellation to the task.
 *                 The cancellation token is passed to the task entry as the second parameter.
 */
CRTDECL(uuid_t, usched_job_queue(usched_task_fn entry, void* argument));

/**
 * @brief Schedules a new task in the scheduler for current execution unit.
 *
 * @param entry    The function to execute with the usched_task_fn signature.
 * @param argument The argument that should be passed to the function.
 * @param params   Configuration parameters for the job
 * @return         A cancellation token value that can be used to signal cancellation to the task.
 *                 The cancellation token is passed to the task entry as the second parameter.
 */
CRTDECL(uuid_t, usched_job_queue3(usched_task_fn entry, void* argument, struct usched_job_parameters* params));

/**
 * @brief Returns the job ID of the currently running job.
 * @return The job ID.
 */
CRTDECL(uuid_t, usched_job_current(void));

/**
 * @brief Initiates a cancellation request for the specified job. The job is not required
 * to react to these requests, and must itself check periodically if anyone has requested
 * a cancellation by using usched_is_cancelled.
 * @param jobID
 * @return
 */
CRTDECL(int, usched_job_cancel(uuid_t jobID));

/**
 * @brief Yields the current job, allowing for other jobs to run on the current
 * execution unit. The current job will run once again any other job has finished
 * running.
 */
CRTDECL(void, usched_job_yield(void));

/**
 * @brief Marks the currently running task as cancelled. The cancelation token will be
 * signaled.
 */
CRTDECL(void, usched_job_exit(int exitCode));

/**
 * @brief Detaches the job with the specified ID. This means that it will move
 * to a seperate, unique execution unit, where only the specified job is allowed
 * execution. This means it will not take up one of the execution units in the pool
 * and instead be isolated in it's own, kernel-managed thread.
 * @param jobID The ID of the job that should be detached.
 * @return 0 if the detachment was succesful.
 *         -1 On any errors, consult errno.
 */
CRTDECL(int, usched_job_detach(uuid_t jobID));

/**
 * @brief Blocks the current job until the requested job has finished execution. The
 * exit code of that job will be then be returned.
 * @param jobID The ID of the job to wait for.
 * @param exitCode The exit code of the job will be placed here.
 * @return 0 If the job finished execution.
 *         -1 On any errors, consult errno.
 */
CRTDECL(int, usched_job_join(uuid_t jobID, int* exitCode));

/**
 * @brief Sends a signal to a job.
 * @param jobID The job ID that should receive the signal.
 * @param signal The signal that should be sent.
 * @return 0 If the signal was sent.
 *         -1 On any errors, consult errno.
 */
CRTDECL(int, usched_job_signal(uuid_t jobID, int signal));

/**
 * @brief Sleeps the current job until the UTC-based time point has been reached.
 * @param until An UTC-based time_point that the job will sleep until.
 * @return 0 If the sleep was done, -1 on any errors.
 */
CRTDECL(int, usched_job_sleep(const struct timespec* until));

/**
 * @brief Used for checking by the currently running job if anyone has requested it
 * to be cancelled. Cancellation policy is co-operative, so that means the job is not
 * required to respect any cancellation requests, but rather should be used on a
 * per-application basis. The scheduling system will never request a job to be cancelled.
 * @param cancellationToken The cancellation token that is provided on job entry.
 * @return True if a cancellation request is pending. Otherwise false.
 */
CRTDECL(bool, usched_is_cancelled(const void* cancellationToken));

_CODE_END
#endif //!__OS_USCHED_JOB_H__
