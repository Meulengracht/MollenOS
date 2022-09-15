/**
 * Copyright 2022, Philip Meulengracht
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

#include <assert.h>
#include <os/usched/job.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include "private.h"

// Needed to handle thread stuff now on an userspace basis
CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

void usched_job_parameters_init(struct usched_job_parameters* params)
{
    params->stack_size = 4096 * 4;
    params->detached = false;
    params->affinity_mask = NULL;
}

static void __finalize_task(struct usched_job* job, int exitCode)
{
    job->state = JobState_FINISHING;

    // Before yielding, let us run the deinitalizers before we do any task cleanup.
    __cxa_threadfinalize();
    usched_yield(NULL);
}

// TaskMain takes care of C/C++ handlers for the thread. Each job is in itself a
// full thread (atleast treated like that), except that it exclusively exists in
// userspace. This function encapsulates the C/C++ handler handling, while the
// TLS for each thread is taken care of by job creation/destruction.
void __usched_task_main(struct usched_job* job)
{
    // Set our state to running, as the task is now in progress
    job->state = JobState_RUNNING;

    // Run any C/C++ initialization for the thread. Before this call
    // the tls must be set correctly. The TLS is set before the jump to this
    // entry function
    __cxa_threadinitialize();

    job->entry(job->argument);
    __finalize_task(job, 0);
}

uuid_t usched_job_queue3(usched_task_fn entry, void* argument, struct usched_job_parameters* params)
{
    struct usched_job* job;

    assert(params != NULL);
    assert(params->stack_size >= 4096);

    job = malloc(sizeof(struct usched_job));
    if (!job) {
        return UUID_INVALID;
    }

    job->stack = malloc(params->stack_size);
    if (!job->stack) {
        free(job);
        return UUID_INVALID;
    }

    job->stack_size = params->stack_size;
    job->state = JobState_CREATED;
    job->next = NULL;
    job->entry = entry;
    job->argument = argument;
    if (__tls_initialize(&job->tls)) {
        free(job->stack);
        free(job);
        return UUID_INVALID;
    }

    // We have two possible ways of queue jobs. Detached jobs get their own
    // execution unit, and thus we queue these through the xunit system. Normal
    // undetached jobs are running in the global job pool
    if (params->detached) {
        if (__xunit_start_detached(job, params)) {
            __tls_destroy(&job->tls);
            free(job->stack);
            free(job);
            return UUID_INVALID;
        }
    } else {
        __usched_add_job_ready(job);
    }
    return job;
}

uuid_t usched_job_queue(usched_task_fn entry, void* argument)
{
    struct usched_job_parameters defaultParams;
    usched_job_parameters_init(&defaultParams);
    return usched_job_queue3(entry, argument, &defaultParams);
}

uuid_t usched_job_current(void) {
    struct usched_scheduler* sched = __usched_get_scheduler();
    if (sched->current == NULL) {
        return UUID_INVALID;
    }
    return sched->current->id;
};

void usched_job_yield(void) {
    usched_yield(NULL);
}

void usched_job_exit(int exitCode)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    if (sched->current == NULL) {
        return;
    }
    __finalize_task(sched->current, exitCode);
}

int usched_job_detach(uuid_t jobID)
{
    // We need to determine the current state of the task, and we do this
    // by first locking the ready queue to assert no new tasks are queued
    // or dequeued while we perform this operation. And because of this, this
    // operation can be a bit expensive to perform, and it is much preferred that
    // this is requested on job-creation.

    // The state can be in one of *several* states at this point, and narrowing down
    // exactly what is happening with this task, while it's not already en-route an
    // operation is going to be extremely tricky.

    // When detaching, the following requirements are in place
    // i. Must not already be detached
    // ii. Must not have the state _FINISHING
    // Furthermore, we have 3 general cases
    // 1. We are trying to detach the current job
    // This is the easy one, we can simply remove any references to this job, queue it up
    // normally and then yield to the next
    // 2. We are trying to detach a job that is not currently running
    // OK so this one could be easy as-well, we could flag this task for detaching whenever
    // it is queued up for running again, which hopefully should be shortly. In any case this job
    // could be in many states (in ready queue, sleeping or blocked).
    // 3. We are trying to detach a currently running job
    // Now this can be problematic, especially if we expect the job to not yield. The strategy
    // here will be to identify the running execution unit, and signal it to migrate the job. Before this
    // we should perform identical action as to (2) to make sure any race-condition is prevented, so as
    // if the execution unit should yield before the signal is received, action would have already been
    // taken to migrate the job. The issue here again, is that we have to protect the state of jobs with
    // synchronization.
}

int usched_job_join(uuid_t jobID, int* exitCode)
{
    // Joining is straight forward. Get the job from the job-table, check it's current
    // state, and wait in the wait-queue. The wait-queue is a regular uthread mtx/cond combo
    // which will wake us up once the job quits.
}

int usched_job_signal(uuid_t jobID, int signal)
{
    // Signalling jobs is a bit more tricky.
}

int usched_job_sleep(const struct timespec* duration, struct timespec* remaining)
{

}
