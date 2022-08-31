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
 */

#include <errno.h>
#include <os/usched/tls.h>
#include <os/usched/usched.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <threads.h>
#include "private.h"

// Needed to handle thread stuff now on a userspace basis
CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

static atomic_int g_timerid = ATOMIC_VAR_INIT(1);

struct usched_scheduler* __usched_get_scheduler(void) {
    return __usched_xunit_tls_current()->scheduler;
}

static clock_t
__get_timestamp_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (ts.tv_sec * MSEC_PER_SEC) + (ts.tv_nsec / NSEC_PER_MSEC);
}

void
usched_init(void)
{
    struct usched_scheduler* sched = __usched_get_scheduler();

    if (sched->magic == SCHEDULER_MAGIC) {
        return;
    }

    // initialize the global scheduler, there will always only exist one
    // scheduler instance, unless we implement multiple executors
    memset(sched, 0, sizeof(struct usched_scheduler));
    mtx_init(&sched->lock, mtx_plain);
    sched->tls   = usched_tls_current();
    sched->magic = SCHEDULER_MAGIC;
}

static struct usched_job*
__get_next_ready(struct usched_scheduler* scheduler)
{
    struct usched_job* next = scheduler->ready;

    if (!scheduler->ready) {
        return NULL;
    }

    scheduler->ready = next->next;
    next->next = NULL;
    return next;
}

// TaskMain takes care of C/C++ handlers for the thread. Each job is in itself a
// full thread (atleast treated like that), except that it exclusively runs in
// userspace. This function encapsulates the C/C++ handler handling, while the
// TLS for each thread is taken care of by job creation/destruction.
void
TaskMain(struct usched_job* job)
{
    // Run any C/C++ initialization for the thread. Before this call
    // the tls must be set correctly. The TLS is set before the jump to this
    // entry function
    __cxa_threadinitialize();

    // Now update the state, and call the entry function.
    job->state = JobState_RUNNING;
    job->entry(job->argument, job);
    job->state = JobState_FINISHING;

    // Before yielding, let us run the deinitalizers before we do any task cleanup.
    __cxa_threadfinalize();
    usched_yield();
}

static void
__switch_task(struct usched_scheduler* sched, struct usched_job* current, struct usched_job* next)
{
    char* stack;

    // save the current context and set a return point
    if (current) {
        if (setjmp(current->context)) {
            return;
        }
    }

    // return to scheduler context if we have no next
    if (!next) {
        // This automatically restores the correct TLS context after the jump
        longjmp(sched->context, 1);
    }

    // Before jumping here, we *must* restore the TLS context as we have
    // no control over the next step.
    __usched_tls_switch(&next->tls);

    // if the thread we want to switch to already has a valid jmp_buf then
    // we can just longjmp into that context
    if (next->state != JobState_CREATED) {
        longjmp(next->context, 1);
    }

    // First time we initalize a context we must manually switch the stack
    // pointer and call the correct entry.
    stack = (char*)next->stack + next->stack_size;
#if defined(__amd64__)
    __asm__ (
            "movq %0, %%rcx; movq %1, %%rsp; callq TaskMain\n"
            :: "r"(next), "r"(stack)
            : "rdi", "rsp", "memory");
#elif defined(__i386__)
    __asm__ (
            "movl %0, %%eax; movl %1, %%esp; pushl %%eax; call _TaskMain\n"
            :: "r"(next), "r"(stack)
            : "eax", "esp", "memory");
#else
#error "Unimplemented architecture for userspace scheduler"
#endif
}

static void
__task_destroy(struct usched_job* job)
{
    __usched_tls_destroy(&job->tls);
    free(job->stack);
    free(job);
}

static void
__empty_garbage_bin(struct usched_scheduler* sched)
{
    struct usched_job* i;

    mtx_lock(&sched->lock);
    i = sched->garbage_bin;
    while (i) {
        struct usched_job* next = i->next;
        __task_destroy(i);
        i = next;
    }
    sched->garbage_bin = NULL;
    mtx_unlock(&sched->lock);
}

static void
__update_timers(struct usched_scheduler* sched)
{
    clock_t                currentTime;
    struct usched_timeout* timer;

    mtx_lock(&sched->lock);
    currentTime = __get_timestamp_ms();
    timer = sched->timers;
    while (timer) {
        if (timer->deadline <= currentTime) {
            timer->active = 0;
            __usched_cond_notify_job(timer->signal, timer->job);
        }
        timer = timer->next;
    }
    mtx_unlock(&sched->lock);
}

static int
__get_next_deadline(struct usched_scheduler* sched)
{
    clock_t                currentTime;
    struct usched_timeout* timer;
    int                    shortest = INT_MAX;

    mtx_lock(&sched->lock);
    currentTime = __get_timestamp_ms();
    timer = sched->timers;
    while (timer) {
        if (timer->active) {
            int diff = (int)(timer->deadline > currentTime ? (timer->deadline - currentTime) : 0);
            shortest = MIN(diff, shortest);
            if (!shortest) {
                break;
            }
        }
        timer = timer->next;
    }
    mtx_unlock(&sched->lock);
    return shortest;
}

int
usched_yield(void)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_job*       current;
    struct usched_job*       next;

    // update timers before we check the scheduler as we might trigger a job to
    // be ready
    __update_timers(sched);

    if (!sched->current) {
        // if no active thread and no ready threads then we can safely just return
        if (!sched->ready) {
            return __get_next_deadline(sched);
        }

        // we are running in scheduler context, make sure we store
        // this context, so we can return to here when we run out of tasks
        // to execute
        if (setjmp(sched->context)) {
            // We are back into the execution unit context, which means we should update
            // the TLS accordingly.
            __usched_tls_switch(sched->tls);

            // Run maintinence tasks before returning the deadline for the next job
            __empty_garbage_bin(sched);
            return __get_next_deadline(sched);
        }
    }

    current = sched->current;

    mtx_lock(&sched->lock);
    if (current) {
        if (SHOULD_RESCHEDULE(current)) {
            AppendJob(&sched->ready, current);
        }
        else if (current->state == JobState_FINISHING) {
            AppendJob(&sched->garbage_bin, current);
        }
    }
    next = __get_next_ready(sched);
    sched->current = next;
    mtx_unlock(&sched->lock);

    // Should always be the last call
    __switch_task(sched, current, next);
    return 0;
}

void usched_wait(void)
{

}

void*
usched_task_queue(usched_task_fn entry, void* argument)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_job*       job;

    job = malloc(sizeof(struct usched_job));
    if (!job) {
        errno = ENOMEM;
        return NULL;
    }

    job->stack = malloc(4096 * 4);
    if (!job->stack) {
        free(job);
        errno = ENOMEM;
        return NULL;
    }

    job->stack_size = 4096 * 4;
    job->state = JobState_CREATED;
    job->next = NULL;
    job->entry = entry;
    job->argument = argument;
    job->cancelled = 0;
    __usched_tls_init(&job->tls);
    AppendJob(&sched->ready, job);

    return job;
}

void usched_task_cancel_current(void)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    if (sched->current == NULL) {
        return;
    }
    usched_task_cancel(sched->current);
}

void
usched_task_cancel(void* cancellationToken)
{
    if (!cancellationToken) {
        return;
    }

    ((struct usched_job*)cancellationToken)->cancelled = 1;
}

int
usched_ct_is_cancelled(void* cancellationToken)
{
    if (!cancellationToken) {
        return 0;
    }

    return ((struct usched_job*)cancellationToken)->cancelled;
}

int
__usched_timeout_start(unsigned int timeout, struct usched_cnd* cond)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_timeout*   timer;

    if (!timeout) {
        errno = EINVAL;
        return -1;
    }

    timer = malloc(sizeof(struct usched_timeout));
    if (!timer) {
        errno = ENOMEM;
        return -1;
    }

    timer->id = atomic_fetch_add(&g_timerid, 1);
    timer->deadline = __get_timestamp_ms() + timeout;
    timer->signal = cond;
    timer->next = NULL;

    mtx_lock(&sched->lock);
    timer->job = sched->current;
    AppendTimer(&sched->timers, timer);
    mtx_unlock(&sched->lock);

    return timer->id;
}

int
__usched_timeout_finish(int id)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_timeout*   timer;
    struct usched_timeout*   previousTimer = NULL;
    int                      result = 0;
    if (id == -1) {
        // an invalid id was provided, ignore it
        return 0;
    }

    mtx_lock(&sched->lock);
    timer = sched->timers;
    while (timer) {
        if (timer->id == id) {
            if (previousTimer) {
                previousTimer->next = timer->next;
            }
            else {
                sched->timers = timer->next;
            }

            // return -1 if timer was signalled.
            if (!timer->active) {
                errno = ETIME;
                result = -1;
            }
            free(timer);
            break;
        }

        previousTimer = timer;
        timer = timer->next;
    }
    mtx_unlock(&sched->lock);
    return result;
}
