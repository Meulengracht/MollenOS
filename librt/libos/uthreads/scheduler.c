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
#include <os/mutex.h>
#include <os/condition.h>
#include <os/usched/usched.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include "private.h"

static atomic_int         g_timerid        = ATOMIC_VAR_INIT(1);
static struct usched_job* g_readyQueue     = NULL;
static Mutex_t            g_readyQueueLock = MUTEX_INIT(MUTEX_PLAIN);
static Condition_t        g_readyQueueCond = COND_INIT;

struct usched_scheduler* __usched_get_scheduler(void) {
    return __usched_xunit_tls_current()->scheduler;
}

void __usched_init(struct usched_scheduler* sched, struct usched_init_params* params)
{
    if (sched->magic == SCHEDULER_MAGIC) {
        return;
    }

    // initialize the global scheduler, there will always only exist one
    // scheduler instance, unless we implement multiple executors
    memset(sched, 0, sizeof(struct usched_scheduler));
    sched->tls   = __tls_current();
    sched->magic = SCHEDULER_MAGIC;

    // If this is set non-null, this means the scheduler will *only* execute
    // tasks from the internal queue instead of the detached queue. This is useful
    // if a user wants to keep long-running tasks OR bind tasks to specific cores,
    // without that impacting the entire workpool.
    if (params->detached_job != NULL) {
        sched->internal_queue = params->detached_job;
        sched->detached       = true;
    }
}

static void
__task_destroy(struct usched_job* job)
{
    __tls_destroy(&job->tls);
    free(job->stack);
    free(job);
}

static void
__empty_garbage_bin(struct usched_scheduler* sched)
{
    struct usched_job* i;

    i = sched->garbage_bin;
    while (i) {
        struct usched_job* next = i->next;
        __task_destroy(i);
        i = next;
    }
    sched->garbage_bin = NULL;
}

void __usched_add_job_ready(struct usched_job* job)
{
    MutexLock(&g_readyQueueLock);
    __usched_append_job(&g_readyQueue, job);
    ConditionSignal(&g_readyQueueCond);
    MutexUnlock(&g_readyQueueLock);
}

int __usched_prepare_migrate(void)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    if (!sched->current) {
        return 0; // no current task, which means we can chill
    }

    // Store the context of the current, this function is called by signal
    // handler, but stays in the same stack as the task, so we can safely store
    // the current context and just return to it later.
    if (setjmp(sched->current->context)) {
        return 1;
    }

    // Move the current task back into the ready-queue
    __usched_add_job_ready(sched->current);

    // Swap back into scheduler context (as much as possible)
    __tls_switch(sched->tls);

    // Run maintinence tasks before returning the deadline for the next job
    __empty_garbage_bin(sched);
    return 0;
}

static struct usched_job*
__get_next_ready(struct usched_scheduler* scheduler)
{
    struct usched_job* next;

    // If a scheduler is detached, then it cannot consume jobs from
    // the global queue. Instead, we only check the internal queue for
    // jobs.
    if (scheduler->detached) {
        next = scheduler->internal_queue;
        if (next != NULL) {
            scheduler->internal_queue = next->next;
            next->next = NULL;
        }
        return next;
    }

    // Otherwise, the execution unit is running as a part of the global
    // worker pool, they only execute from the global ready queue
    MutexLock(&g_readyQueueLock);
    next = g_readyQueue;
    if (next != NULL) {
        g_readyQueue = next->next;
        next->next = NULL;
    }
    MutexUnlock(&g_readyQueueLock);
    return next;
}

// entry point for new tasks
extern void __usched_task_main(struct usched_job* job);

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
    __tls_switch(&next->tls);

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
            "movq %0, %%rcx; movq %1, %%rsp; callq __usched_task_main\n"
            :: "r"(next), "r"(stack)
            : "rdi", "rsp", "memory");
#elif defined(__i386__)
    __asm__ (
            "movl %0, %%eax; movl %1, %%esp; pushl %%eax; call ___usched_task_main\n"
            :: "r"(next), "r"(stack)
            : "eax", "esp", "memory");
#else
#error "Unimplemented architecture for userspace scheduler"
#endif
}

static void __notify_timer(struct usched_timeout* timer)
{
    if (timer->queue_type == __QUEUE_TYPE_SLEEP) {
        __usched_job_notify(timer->job);
    } else if (timer->queue_type == __QUEUE_TYPE_MUTEX) {
        __usched_mtx_notify_job(timer->queue.mutex, timer->job);
    } else if (timer->queue_type == __QUEUE_TYPE_COND) {
        __usched_cond_notify_job(timer->queue.cond, timer->job);
    }
}

static bool __is_before_or_equal(const struct timespec* before, const struct timespec* this) {
    if (before->tv_sec < this->tv_sec) {
        return true;
    } else if (before->tv_sec == this->tv_sec) {
        return before->tv_nsec <= this->tv_nsec;
    }
    return false;
}

static void
__update_timers(struct usched_scheduler* sched)
{
    struct timespec        currentTime;
    struct usched_timeout* timer;

    timespec_get(&currentTime, TIME_UTC);
    timer = sched->timers;
    while (timer) {
        if (__is_before_or_equal(&timer->deadline, &currentTime)) {
            timer->active = 0;
            __notify_timer(timer);
        }
        timer = timer->next;
    }
}

static int __get_next_deadline(
        struct usched_scheduler* sched,
        struct timespec*         deadline)
{
    struct timespec        currentTime;
    struct timespec        currentDiff;
    struct usched_timeout* timer;
    clock_t                shortest = (clock_t)-1;
    int                    result   = -1;

    timespec_get(&currentTime, TIME_UTC);
    timer = sched->timers;
    while (timer) {
        if (timer->active) {
            if (__is_before_or_equal(&timer->deadline, &currentTime)) {
                // timer ready, let it run again
                currentDiff.tv_sec  = 0;
                currentDiff.tv_nsec = 0;
            } else {
                timespec_diff(&currentTime, &timer->deadline , &currentDiff);
            }

            clock_t diff = (clock_t)((currentDiff.tv_sec * NSEC_PER_SEC) + (clock_t)currentDiff.tv_nsec);
            if (diff < shortest) {
                deadline->tv_sec = timer->deadline.tv_sec;
                deadline->tv_nsec = timer->deadline.tv_nsec;
            }
            shortest = MIN(diff, shortest);
            if (!shortest) {
                result = 0;
                break;
            }
        }
        timer = timer->next;
    }

    // Set error code based on the outcome of this. We must inform
    // the execution unit of the next action based on when a new job
    // is ready
    if (shortest == (clock_t)-1) {
        // No entries, this means the XU should block indefinitely
        // untill a new job enters ready queue
        errno = ENOENT;
    } else {
        // Entries will be ready at some point, and the XU should check
        // when the deadline has been reached.
        errno = EWOULDBLOCK;
    }
    return result;
}

int usched_yield(struct timespec* deadline)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_job*       current;
    struct usched_job*       next;

    // update timers before we check the scheduler as we might trigger a job to
    // be ready
    __update_timers(sched);

    next = __get_next_ready(sched);
    if (sched->current == NULL) {
        // if no active thread and no ready threads then we can safely just return
        if (next == NULL) {
            return __get_next_deadline(sched, deadline);
        }

        // we are running in scheduler context, make sure we store
        // this context, so we can return to here when we run out of tasks
        // to execute
        if (setjmp(sched->context)) {
            // We are back into the execution unit context, which means we should update
            // the TLS accordingly.
            __tls_switch(sched->tls);

            // Run maintinence tasks before returning the deadline for the next job
            __empty_garbage_bin(sched);
            return __get_next_deadline(sched, deadline);
        }
    }

    current = sched->current;
    if (current) {
        if (SHOULD_RESCHEDULE(current)) {
            // let us skip the whole schedule unschedule if
            // possible.
            if (next == NULL) {
                next = current;
            } else {
                __usched_add_job_ready(current);
            }
        } else if (current->state == JobState_FINISHING) {
            __usched_append_job(&sched->garbage_bin, current);
        }
    }
    sched->current = next;

    // Should always be the last call
    __switch_task(sched, current, next);
    return 0;
}

void usched_timedwait(const struct timespec* until)
{
    MutexLock(&g_readyQueueLock);
    while (g_readyQueue == NULL) {
        oserr_t oserr = ConditionTimedWait(&g_readyQueueCond, &g_readyQueueLock, until);
        if (oserr == OsTimeout) {
            break;
        }
    }
    MutexUnlock(&g_readyQueueLock);
}

void usched_wait(void)
{
    MutexLock(&g_readyQueueLock);
    while (g_readyQueue == NULL) {
        ConditionWait(&g_readyQueueCond, &g_readyQueueLock);
    }
    MutexUnlock(&g_readyQueueLock);
}

int __usched_timeout_start(const struct timespec *restrict until, union usched_timer_queue* queue, int queueType)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_timeout*   timer;

    if (!until) {
        errno = EINVAL;
        return -1;
    }

    timer = malloc(sizeof(struct usched_timeout));
    if (!timer) {
        errno = ENOMEM;
        return -1;
    }

    timer->id = atomic_fetch_add(&g_timerid, 1);
    timer->deadline.tv_sec = until->tv_sec;
    timer->deadline.tv_nsec = until->tv_nsec;
    timer->queue = *queue;
    timer->queue_type = queueType;
    timer->next = NULL;

    timer->job = sched->current;
    __usched_append_timer(&sched->timers, timer);

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
    return result;
}
