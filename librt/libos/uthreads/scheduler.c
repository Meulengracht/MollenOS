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

//#define __TRACE
#define __need_minmax

#include <assert.h>
#include <errno.h>
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/mutex.h>
#include <os/usched/usched.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include "private.h"

static atomic_int g_timerid = 1;
static struct usched_scheduler g_scheduler = {
    .lock = MUTEX_INIT(MUTEX_PLAIN)
};

struct usched_execution_context*
__usched_get_execution(void)
{
    return __usched_xunit_tls_current()->context;
}

static struct usched_scheduler*
__scheduler_new(void)
{
    struct usched_scheduler* scheduler;

    scheduler = calloc(1, sizeof(struct usched_scheduler));
    if (scheduler == NULL) {
        return NULL;
    }

    MutexInitialize(&scheduler->lock, MUTEX_PLAIN);
    if (OSHandleCreate(NULL, 0, &scheduler->notification_handle) != OS_EOK) {
        free(scheduler);
        return NULL;
    }
    return scheduler;
}

static oserr_t
__init_notification_queue(
    _InOut_ struct usched_execution_context* execution)
{
    oserr_t oserr;

    oserr = OSNotificationQueueCreate(0, &execution->notification_queue);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = OSHandleCreate(NULL, 0, &execution->syscall_handle);
    if (oserr != OS_EOK) {
        OSHandleDestroy(&execution->notification_queue);
        return oserr;
    }

    oserr = OSNotificationQueueCtrl(
        &execution->notification_queue,
        IOSET_ADD,
        &execution->scheduler->notification_handle,
        &(struct ioset_event) {
            .events = IOSETSYN,
            .data.handle = execution->scheduler->notification_handle.ID
        }
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = OSNotificationQueueCtrl(
        &execution->notification_queue,
        IOSET_ADD,
        &execution->syscall_handle,
        &(struct ioset_event) {
            .events = IOSETSYN,
            .data.handle = execution->syscall_handle.ID
        }
    );
    if (oserr == OS_EOK) {
        return OS_EOK;
    }

error:
    OSHandleDestroy(&execution->syscall_handle);
    OSHandleDestroy(&execution->notification_queue);
    return oserr;
}

void
__usched_startup(void)
{
    if (OSHandleCreate(NULL, 0, &g_scheduler.notification_handle) != OS_EOK) {
        exit(-1);
    }
}

void
__usched_init(
    _InOut_ struct usched_execution_context* execution,
    _In_    struct usched_init_params*       params)
{
    if (execution->magic == SCHEDULER_MAGIC) {
        return;
    }

    // The caller zero-initializes the context before publishing the unit.
    // Do not clear a stop request that arrived while its kernel thread started.
    execution->tls = __tls_current();
    execution->scheduler = &g_scheduler;
    if (params->detached_job != NULL) {
        execution->scheduler = __scheduler_new();
        if (execution->scheduler == NULL) {
            exit(-1);
        }
        params->detached_job->scheduler = execution->scheduler;
        __usched_add_job_ready(params->detached_job);
    }

    if (__init_notification_queue(execution) != OS_EOK) {
        exit(-1);
    }
    execution->magic = SCHEDULER_MAGIC;
}

void
__usched_destroy(
    _InOut_ struct usched_execution_context* execution)
{
    if (execution->magic != SCHEDULER_MAGIC) {
        return;
    }

    OSHandleDestroy(&execution->notification_queue);
    OSHandleDestroy(&execution->syscall_handle);
    if (execution->scheduler != &g_scheduler) {
        OSHandleDestroy(&execution->scheduler->notification_handle);
        free(execution->scheduler);
    }
    execution->magic = 0;
}

void
__usched_request_stop(
    _InOut_ struct usched_execution_context* execution)
{
    // Only pooled units are resized. The shared handle is valid even when the
    // target unit has not initialized its notification queue yet.
    atomic_store(&execution->stop_requested, true);
    OSNotificationQueuePost(&g_scheduler.notification_handle, IOSETSYN);
}

static void
__task_destroy(
    _In_ struct usched_job* job)
{
    // Finalizers ran on the job stack before it returned to the scheduler.
    __tls_destroy(&job->tls);
    free(job->stack);
    free(job);
}

static void
__queue_job_locked(
    _In_ struct usched_job* job)
{
    // scheduler->Lock must be held, and no execution unit may use this stack.
    assert(__JOB_STATE(job) == JobState_CREATED || __JOB_STATE(job) == JobState_READY);
    __usched_append_job(&job->scheduler->ready, job);
    OSNotificationQueuePost(&job->scheduler->notification_handle, IOSETSYN);
}

void
__usched_add_job_ready(
    _In_ struct usched_job* job)
{
    if (job->scheduler == NULL) {
        job->scheduler = &g_scheduler;
    }

    MutexLock(&job->scheduler->lock);
    __queue_job_locked(job);
    MutexUnlock(&job->scheduler->lock);
}

void
__usched_job_block(
    _InOut_ struct usched_job* job)
{
    MutexLock(&job->scheduler->lock);
    assert(__JOB_STATE(job) == JobState_RUNNING);
    __JOB_SET_STATE(job, JobState_BLOCKING);
    MutexUnlock(&job->scheduler->lock);
}

void
__usched_job_ready(
    _In_ struct usched_job* job)
{
    struct usched_scheduler* scheduler;

    scheduler = job->scheduler;
    MutexLock(&scheduler->lock);
    job->next = NULL;

    // An early notification only cancels the block. The job is still on its
    // stack, so __finish_dispatch publishes it once the stack is left.
    if (__JOB_STATE(job) == JobState_BLOCKING) {
        __JOB_SET_STATE(job, JobState_RUNNING);
    } else {
        assert(__JOB_STATE(job) == JobState_BLOCKED);
        __JOB_SET_STATE(job, JobState_READY);
        __queue_job_locked(job);
    }
    MutexUnlock(&scheduler->lock);
}

static void
__finish_dispatch(
    _InOut_ struct usched_execution_context* execution)
{
    struct usched_scheduler* scheduler;
    struct usched_job*       job;
    enum job_state           state;

    scheduler = execution->scheduler;
    job = execution->current;
    execution->current = NULL;

    // The job has left its stack, so move it to the matching off-stack state.
    MutexLock(&scheduler->lock);
    state = __JOB_STATE(job);
    if (state == JobState_RUNNING) {
        __JOB_SET_STATE(job, JobState_READY);
        __queue_job_locked(job);
    } else if (state == JobState_BLOCKING) {
        __JOB_SET_STATE(job, JobState_BLOCKED);
    } else {
        assert(state == JobState_FINISHING);
    }
    MutexUnlock(&scheduler->lock);

    // A published job may already have resumed on another unit. Only a
    // finished job can still be accessed here, including freeing its stack.
    if (state == JobState_FINISHING) {
        __task_destroy(job);
    }
}

static struct usched_job*
__get_next_ready(
    _In_ struct usched_scheduler* scheduler)
{
    struct usched_job* next;

    MutexLock(&scheduler->lock);
    next = scheduler->ready;
    if (next != NULL) {
        scheduler->ready = next->next;
        next->next = NULL;

        // A job that has never run must enter through the task entry.
        if (__JOB_STATE(next) == JobState_CREATED) {
            __JOB_SET_STATE(next, JobState_STARTING);
        } else {
            assert(__JOB_STATE(next) == JobState_READY);
            __JOB_SET_STATE(next, JobState_RUNNING);
        }
    }
    MutexUnlock(&scheduler->lock);
    return next;
}

// entry point for new tasks
extern void __usched_task_main(struct usched_job* job);
#if defined(__aarch64__)
extern _Noreturn void __usched_task_start(void* stack, struct usched_job* job,
                                        void (*entry)(struct usched_job*));
#endif

// Every dispatch (including same-job resumption and migration) must catch up
// before entering user code. Only the selected job owns these TLS slots; this
// does not publish modules to jobs already running on other execution units.
static void __prepare_task_entry(struct usched_job* job)
{
    __tls_switch(&job->tls);
    job->tls.job_id = job->id;
    job->tls.async_context = &job->async_context;

    // Storage preparation is incremental and leaves existing blocks intact.
    // Do not run DLL attach callbacks here. They need a separate lifecycle and
    // reentrancy policy. Never resume a job with a partially prepared TLS array.
    if (__tls_prepare_modules()) {
        __builtin_trap();
    }
}

static void _Noreturn __switch_task(
    struct usched_job* job)
{
    char* stack;
    TRACE("__switch_task(job=%u)", job->id);

    // Prepare the task's TLS and module state before switching to its context.
    __prepare_task_entry(job);

    // if the thread we want to switch to already has a valid jmp_buf then
    // we can just longjmp into that context
    if (__JOB_STATE(job) != JobState_STARTING) {
        longjmp(job->context, 1);
    }

    // First time we initalize a context we must manually switch the stack
    // pointer and call the correct entry.
    stack = (char*)job->stack + job->stack_size;
#if defined(__aarch64__)
    __usched_task_start(stack, job, __usched_task_main);
#elif defined(__amd64__)
    __asm__ (
            "movq %0, %%rcx; movq %1, %%rsp; callq __usched_task_main\n"
            :: "r"(job), "r"(stack)
            : "rdi", "rsp", "memory");
#elif defined(__i386__)
    __asm__ (
            "movl %0, %%eax; movl %1, %%esp; pushl %%eax; call ___usched_task_main\n"
            :: "r"(job), "r"(stack)
            : "eax", "esp", "memory");
#else
#error "Unimplemented architecture for userspace scheduler"
#endif
    for (;;);
}

static void __notify_timer(
        struct usched_timeout* timer)
{
    TRACE("__notify_timer(job=%u)", timer->job->id);
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

static void __update_timers(
        struct usched_execution_context* execution)
{
    struct timespec        currentTime;
    struct usched_timeout* timer;

    timespec_get(&currentTime, TIME_UTC);
    timer = execution->timers;
    while (timer) {
        if (timer->active) {
            if (__is_before_or_equal(&timer->deadline, &currentTime)) {
                timer->active = 0;
                __notify_timer(timer);
            }
        }
        timer = timer->next;
    }
}

static int __get_next_deadline(
        struct usched_execution_context* execution,
        struct timespec*         deadline)
{
    struct timespec        currentTime;
    struct timespec        currentDiff;
    struct usched_timeout* timer;
    clock_t                shortest = (clock_t)-1;
    int                    result   = -1;

    timespec_get(&currentTime, TIME_UTC);
    timer = execution->timers;
    while (timer) {
        if (timer->active) {
            if (__is_before_or_equal(&timer->deadline, &currentTime)) {
                // timer ready, let it run again
                currentDiff.tv_sec  = 0;
                currentDiff.tv_nsec = 0;
            } else {
                timespec_diff(&timer->deadline, &currentTime,  &currentDiff);
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

static void
__process_syscall_completions(
    _InOut_ struct usched_execution_context* execution)
{
    struct usched_syscall* i, *p, *next;

    i = execution->syscalls_pending;
    p = NULL;
    while (i) {
        // The completed job may resume and leave this stack frame on another
        // unit as soon as it is published. Save the link before that point.
        next = i->next;

        // TODO: This should be read atomically as the status is updated from
        // *maybe* another core. Even though we should probably always execute
        // these kinds of syscalls on the same core? (We can consider this)
        if (i->async_context->ErrorCode != OS_ESCSTARTED) {
            // unlink it and schedule the job
            if (p == NULL) {
                execution->syscalls_pending = i->next;
            } else {
                p->next = i->next;
            }
            __usched_job_ready(i->job);
        } else {
            p = i;
        }

        i = next;
    }
}

int
usched_yield(
    _Out_Opt_ struct timespec* deadline)
{
    struct usched_execution_context* execution;
    struct usched_job*               current;
    struct usched_job*               next;

    execution = __usched_get_execution();
    current = execution->current;
    if (current != NULL) {
        if (setjmp(current->context) == 0) {
            longjmp(execution->jmp_context, 1);
        }

        // Dispatch restored job TLS. The saved execution pointer may refer
        // to another unit after migration, so do not use it on resumption.
        return 0;
    }

    // This continuation stays on the kernel thread's scheduler stack. Every
    // job returns here before its state is published or its stack is freed.
    if (setjmp(execution->jmp_context) != 0) {
        __tls_switch(execution->tls);
        __finish_dispatch(execution);
    }

    // Wait registrations are still local in this first refactor. Keep the
    // unit running until those lists are empty so retirement cannot drop them.
    if (atomic_load(&execution->stop_requested) &&
        execution->timers == NULL && execution->syscalls_pending == NULL) {
        errno = ESHUTDOWN;
        return -1;
    }

    __update_timers(execution);
    __process_syscall_completions(execution);
    next = __get_next_ready(execution->scheduler);
    if (next == NULL) {
        return __get_next_deadline(execution, deadline);
    }

    execution->current = next;
    __switch_task(next);
}

void usched_timedwait(const struct timespec* until)
{
    struct usched_execution_context* execution = __usched_get_execution();
    struct ioset_event               events[2];
    int                              numEvents;
    oserr_t                          oserr;
    
    TRACE("usched_timedwait()");

    // We have two handles in the notification queue, so we can be supplied
    // with *at most* two events. We can actually in detail detect which type
    // of event occurred, but we do not care.
    oserr = OSNotificationQueueWait(
        &execution->notification_queue,
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

void usched_wait(void) {
    usched_timedwait(NULL);
}

int __usched_timeout_start(const struct timespec *restrict until, union usched_timer_queue* queue, int queueType)
{
    struct usched_execution_context* execution = __usched_get_execution();
    struct usched_timeout*           timer;

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
    timer->active = 1;
    timer->deadline.tv_sec = until->tv_sec;
    timer->deadline.tv_nsec = until->tv_nsec;
    timer->queue = *queue;
    timer->queue_type = queueType;
    timer->next = NULL;

    timer->job = execution->current;
    __usched_append_timer(&execution->timers, timer);

    return timer->id;
}

int
__usched_timeout_finish(int id)
{
    struct usched_execution_context* execution = __usched_get_execution();
    struct usched_timeout*           timer;
    struct usched_timeout*           previousTimer = NULL;
    int                              result = 0;
    bool                             timerFound = false;

    if (id == -1) {
        // an invalid id was provided, ignore it
        return 0;
    }

    timer = execution->timers;
    while (timer) {
        if (timer->id == id) {
            if (previousTimer) {
                previousTimer->next = timer->next;
            }
            else {
                execution->timers = timer->next;
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

void usched_wait_async(void)
{
    struct usched_execution_context* execution = __usched_get_execution();
    struct usched_job*               current = execution->current;
    struct usched_syscall            syscall = {
            .job = current,
            .async_context = &current->async_context,
            .next = NULL
    };
    TRACE("usched_wait_async()");
    if (execution->syscalls_pending == NULL) {
        execution->syscalls_pending = &syscall;
    } else {
        struct usched_syscall* i = execution->syscalls_pending;
        while (i->next) {
            i = i->next;
        }
        i->next = &syscall;
    }

    // set us blocked, so we won't be rescheduled and then
    // yield for the next job.
    __usched_job_block(execution->current);
    usched_yield(NULL);
}

uuid_t usched_notification_handle(void)
{
    struct usched_execution_context* execution = __usched_get_execution();
    return execution->syscall_handle.ID;
}
