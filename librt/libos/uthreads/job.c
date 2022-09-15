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


void usched_job_parameters_init(struct usched_job_parameters* params)
{
    params->stack_size = 4096 * 4;
    params->detached = false;
    params->affinity_mask = NULL;
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
    job->cancelled = 0;
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
    usched_task_cancel(sched->current);
}

void usched_job_cancel(uuid_t jobID)
{
    if (!cancellationToken) {
        return;
    }

    ((struct usched_job*)cancellationToken)->cancelled = 1;
}

int usched_ct_is_cancelled(void* cancellationToken)
{
    if (!cancellationToken) {
        return 0;
    }

    return ((struct usched_job*)cancellationToken)->cancelled;
}

CRTDECL(int, usched_job_detach(uuid_t jobID));

CRTDECL(int, usched_job_join(uuid_t jobID, int* exitCode));

CRTDECL(int, usched_job_signal(uuid_t jobID, int signal));

CRTDECL(int, usched_job_sleep(const struct timespec* duration, struct timespec* remaining));
