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

#define __need_minmax
#include <assert.h>
#include <errno.h>
#include <ddk/handle.h>
#include <os/mutex.h>
#include <os/usched/usched.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include "private.h"

struct usched_scheduler_queue {
    struct usched_job* ready;
    Mutex_t            mutex;
    uuid_t             notification_handle;
};

static atomic_int                    g_timerid     = ATOMIC_VAR_INIT(1);
static struct usched_scheduler_queue g_globalQueue = { NULL, MUTEX_INIT(MUTEX_PLAIN), UUID_INVALID };

struct usched_scheduler* __usched_get_scheduler(void) {
    return __usched_xunit_tls_current()->scheduler;
}

static struct usched_scheduler_queue* __usched_scheduler_queue_new(struct usched_job* job)
{
    struct usched_scheduler_queue* queue = malloc(sizeof(struct usched_scheduler_queue));
    if (queue == NULL) {
        return NULL;
    }

    queue->ready = job;
    MutexInitialize(&queue->mutex, MUTEX_PLAIN);
    if (OSHandleCreate(&queue->notification_handle) != OS_EOK) {
        free(queue);
        return NULL;
    }
    return queue;
}

static oserr_t __usched_init_notification_queue(struct usched_scheduler* sched)
{
    oserr_t oserr;

    oserr = OSNotificationQueueCreate(0, &sched->notification_queue);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = OSHandleCreate(&sched->syscall_handle);
    if (oserr != OS_EOK) {
        OSHandleDestroy(sched->notification_queue);
    }

    // Either we add the global queue, or we add the internal queue.
    if (sched->internal_queue != NULL) {
        oserr = OSNotificationQueueCtrl(
                sched->notification_queue,
                IOSET_ADD,
                sched->internal_queue->notification_handle,
                &(struct ioset_event) {
                    .events = IOSETSYN,
                    .data.handle = sched->internal_queue->notification_handle
                }
        );
    } else {
        oserr = OSNotificationQueueCtrl(
                sched->notification_queue,
                IOSET_ADD,
                g_globalQueue.notification_handle,
                &(struct ioset_event) {
                        .events = IOSETSYN,
                        .data.handle = g_globalQueue.notification_handle
                }
        );
    }
    if (oserr != OS_EOK) {
        OSHandleDestroy(sched->syscall_handle);
        OSHandleDestroy(sched->notification_queue);
        return oserr;
    }

    oserr = OSNotificationQueueCtrl(
            sched->notification_queue,
            IOSET_ADD,
            sched->syscall_handle,
            &(struct ioset_event) {
                    .events = IOSETSYN,
                    .data.handle = sched->syscall_handle
            }
    );
    if (oserr != OS_EOK) {
        OSHandleDestroy(sched->syscall_handle);
        OSHandleDestroy(sched->notification_queue);
        return oserr;
    }
    return OS_EOK;
}

extern void __usched_startup(void)
{
    oserr_t oserr;

    // Create the handle for the global queue. This will be added to all
    // scheduler's notification queue, so they can track new job postings.
    oserr = OSHandleCreate(&g_globalQueue.notification_handle);
    if (oserr != OS_EOK) {
        exit(-1);
    }
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
        sched->internal_queue = __usched_scheduler_queue_new(params->detached_job);
        assert(sched->internal_queue != NULL);

        // Also bind the job to the internal queue
        params->detached_job->queue = sched->internal_queue;
    }

    // Create a new notification queue for this scheduler. Schedulers will listen for two different
    // types of events; Syscall completions and the global queue posts. If either of these happen the
    // scheduler should wake and act immediately. The global queue will be shared amongst each scheduler
    // and that means a random one will take new jobs posted, and the syscall completion queue will be
    // per-scheduler.
    if (__usched_init_notification_queue(sched) != OS_EOK) {
        // Should not happen, in that case the system ran out of memory, so quit.
        exit(-1);
    }
}

void __usched_destroy(struct usched_scheduler* sched)
{
    if (sched->magic != SCHEDULER_MAGIC) {
        return;
    }
    OSHandleDestroy(sched->notification_queue);
    OSHandleDestroy(sched->syscall_handle);
    if (sched->internal_queue != NULL) {
        OSHandleDestroy(sched->internal_queue->notification_handle);
        free(sched->internal_queue);
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
    struct usched_scheduler_queue* queue = job->queue;

    // When a job is queued for the global queue, then the job will initially have
    // the queue set to NULL. In this case bind it to the global queue.
    if (queue == NULL) {
        job->queue = &g_globalQueue;
        queue = &g_globalQueue;
    }

    MutexLock(&queue->mutex);
    __usched_append_job(&queue->ready, job);
    MutexUnlock(&queue->mutex);
    OSNotificationQueuePost(queue->notification_handle, IOSETSYN);
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
    struct usched_scheduler_queue* queue = &g_globalQueue;
    struct usched_job*             next;

    // If a scheduler has its own internal queue set, then it cannot consume jobs from
    // the global queue. Instead, we only check the internal queue for jobs.
    if (scheduler->internal_queue != NULL) {
        queue = scheduler->internal_queue;
    }

    MutexLock(&queue->mutex);
    next = queue->ready;
    if (next != NULL) {
        queue->ready = next->next;
        next->next = NULL;
    }
    MutexUnlock(&queue->mutex);
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

    // Set up the job id for the current job before running it, These are
    // things are not practical for us to do at job creation, so we have
    // deferred them to just-in-time initialization.
    __tls_current()->job_id = next->id;

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

static void __parse_syscall_completions(
        struct usched_scheduler* sched)
{
    struct usched_syscall* i, *p;

    // Reset the event count back to zero and store the actual number
    // of completions. Then we iterate through the syscalls and check
    // their status
    i = sched->syscalls_pending;
    p = NULL;
    while (i) {
        // TODO: This should be read atomically as the status is updated from
        // *maybe* another core. Even though we should probably always execute
        // these kinds of syscalls on the same core? (We can consider this)
        if (i->async_context->ErrorCode != OS_ESCSTARTED) {
            // unlink it and schedule the job
            if (p == NULL) {
                sched->syscalls_pending = i->next;
            } else {
                p->next = i->next;
            }
            __usched_add_job_ready(i->job);
        }

        p = i;
        i = i->next;
    }
}

int usched_yield(struct timespec* deadline)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_job*       current;
    struct usched_job*       next;

    // update timers before we check the scheduler as we might trigger a job to
    // be ready
    __update_timers(sched);
    __parse_syscall_completions(sched);

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
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct ioset_event       events[2];
    int                      numEvents;
    oserr_t                  oserr;

    // We have two handles in the notification queue, so we can be supplied
    // with *at most* two events. We can actually in detail detect which type
    // of event occurred, but we do not care.
    oserr = OSNotificationQueueWait(
            sched->notification_queue,
            &events[0],
            2,
            0,
            until == NULL ? NULL : &(OSTimestamp_t) {
                .Seconds = until->tv_sec,
                .Nanoseconds = until->tv_nsec
            },
            &numEvents,
            NULL
    );

    // Either it should return timeout or it should return OK. I don't really
    // see the point in doing error detection here, maybe that will change.
    _CRT_UNUSED(oserr);
}

void usched_wait(void)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct ioset_event       events[2];
    int                      numEvents;
    oserr_t                  oserr;

    // We have two handles in the notification queue, so we can be supplied
    // with *at most* two events. We can actually in detail detect which type
    // of event occurred, but we do not care.
    oserr = OSNotificationQueueWait(
            sched->notification_queue,
            &events[0],
            2,
            0,
            NULL,
            &numEvents,
            NULL
    );

    // do not expect any issues from oserr at this point as
    // we do not supply a timeout, nor do we have an async context
    // for this.
    _CRT_UNUSED(oserr);
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
    bool                     timerFound = false;

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
            timerFound = true;
            break;
        }

        previousTimer = timer;
        timer = timer->next;
    }

    if (timerFound) {
        // return -1 if timer was signalled.
        if (!timer->active) {
            errno = ETIME;
            result = -1;
        }
        free(timer);
    }
    return result;
}

void usched_wait_async(OSAsyncContext_t* asyncContext)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    struct usched_syscall    syscall = {
            .job = sched->current,
            .async_context = asyncContext,
            .next = NULL
    };
    if (sched->syscalls_pending == NULL) {
        sched->syscalls_pending = &syscall;
    } else {
        struct usched_syscall* i = sched->syscalls_pending;
        while (i->next) {
            i = i->next;
        }
        i->next = &syscall;
    }

    // set us blocked, so we won't be rescheduled and then
    // yield for the next job.
    sched->current->state = JobState_BLOCKED;
    usched_yield(NULL);
}

uuid_t usched_notification_handle(void)
{
    struct usched_scheduler* sched = __usched_get_scheduler();
    return sched->syscall_handle;
}
