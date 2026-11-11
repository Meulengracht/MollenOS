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
        OSHandleDestroy(&execution->notification_queue);
    }
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

    // The context must be zero-initialized before the unit's kernel thread starts.
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
    if (execution->scheduler != &g_scheduler) {
        assert(execution->scheduler->timers == NULL);
        assert(execution->scheduler->syscalls_pending == NULL);
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
    // scheduler->lock must be held, and no execution unit may use this stack.
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

static void
__job_ready_locked(
    _InOut_ struct usched_job* job)
{
    job->next = NULL;

    // An early notification cancels the block. The job is still using its
    // stack, so __finish_dispatch queues it after it returns to the scheduler.
    if (__JOB_STATE(job) == JobState_BLOCKING) {
        __JOB_SET_STATE(job, JobState_RUNNING);
    } else {
        assert(__JOB_STATE(job) == JobState_BLOCKED);
        __JOB_SET_STATE(job, JobState_READY);
        __queue_job_locked(job);
    }
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

    // The job has returned to the scheduler stack. It can now be queued,
    // blocked or destroyed.
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

    // A queued job may already have resumed on another unit. Only a
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
extern _Noreturn void 
__usched_task_start(
    void* stack,
    struct usched_job* job,
    void (*entry)(struct usched_job*));
#endif

// Update the job's TLS before every dispatch, including when it resumes on
// the same unit. Jobs already running on other units pick up new modules
// when they next resume.
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

static bool
__is_before_or_equal(
    _In_ const struct timespec* before,
    _In_ const struct timespec* after)
{
    if (before->tv_sec == after->tv_sec) {
        return before->tv_nsec <= after->tv_nsec;
    }
    return before->tv_sec < after->tv_sec;
}

static void
__remove_timer_locked(
    _InOut_ struct usched_wait* wait)
{
    struct usched_wait** link;

    if (!wait->timed) {
        return;
    }

    // A wait with timed set must still be in the scheduler timer list.
    link = &wait->job->scheduler->timers;
    while (*link != wait) {
        assert(*link != NULL);
        link = &(*link)->timer_next;
    }
    *link = wait->timer_next;

    // Clear timed so later cleanup skips the list.
    wait->timer_next = NULL;
    wait->timed = false;
}

static bool
__complete_wait_locked(
    _InOut_ struct usched_wait*    wait,
    _In_    enum usched_wait_status result)
{
    // The timer and a notifier can both reach this wait. Keep the first result.
    if (wait->result != UschedWaitStatus_PENDING) {
        return false;
    }

    // The wait is on the job's stack. Remove the timer before waking the job,
    // as it can resume on another unit and leave this stack frame. The job
    // removes any remaining condition or mutex link before returning.
    __remove_timer_locked(wait);
    wait->result = result;
    __job_ready_locked(wait->job);
    return true;
}

void
__usched_wait_prepare(
    _Out_ struct usched_wait*    wait,
    _In_  const struct timespec* until)
{
    struct usched_scheduler* scheduler;

    wait->job = __usched_get_execution()->current;
    wait->result = UschedWaitStatus_PENDING;
    wait->timed = until != NULL;
    wait->timer_next = NULL;
    wait->queue_next = NULL;
    
    scheduler = wait->job->scheduler;
    MutexLock(&scheduler->lock);
    
    assert(__JOB_STATE(wait->job) == JobState_RUNNING);
    
    __JOB_SET_STATE(wait->job, JobState_BLOCKING);
    if (until != NULL) {
        wait->deadline = *until;
        wait->timer_next = scheduler->timers;
        scheduler->timers = wait;

        // Wake up idle units, as they may be sleeping until a later deadline
        // or without any deadline at all.
        OSNotificationQueuePost(&scheduler->notification_handle, IOSETSYN);
    }
    MutexUnlock(&scheduler->lock);
}

void
__usched_wait_enqueue(
    _InOut_ struct usched_wait** queue,
    _InOut_ struct usched_wait*  wait)
{
    while (*queue != NULL) {
        queue = &(*queue)->queue_next;
    }
    *queue = wait;
}

void
__usched_wait_remove(
    _InOut_ struct usched_wait** queue,
    _In_    struct usched_wait*  wait)
{
    while (*queue != NULL) {
        if (*queue == wait) {
            *queue = wait->queue_next;
            return;
        }
        queue = &(*queue)->queue_next;
    }
}

struct usched_job*
__usched_wait_signal(
    _InOut_ struct usched_wait** queue)
{
    struct usched_wait*      wait;
    struct usched_job*       job;
    struct usched_scheduler* scheduler;
    bool                     completed;

    while (*queue != NULL) {
        wait = *queue;
        *queue = wait->queue_next;
        
        job = wait->job;
        
        scheduler = job->scheduler;
        MutexLock(&scheduler->lock);
        completed = __complete_wait_locked(wait, UschedWaitStatus_SIGNALLED);
        MutexUnlock(&scheduler->lock);
        if (completed) {
            return job;
        }
    }
    return NULL;
}

static void
__update_timers(
    _InOut_ struct usched_scheduler* scheduler)
{
    struct timespec      now;
    struct usched_wait** link;
    struct usched_wait*  wait;

    timespec_get(&now, TIME_UTC);

    MutexLock(&scheduler->lock);
    link = &scheduler->timers;
    while (*link != NULL) {
        wait = *link;

        if (__is_before_or_equal(&wait->deadline, &now)) {
            // Unlink the wait here and clear timed, so __complete_wait_locked
            // does not search the timer list for it again.
            *link = wait->timer_next;
            wait->timer_next = NULL;
            wait->timed = false;
            __complete_wait_locked(wait, UschedWaitStatus_TIMEOUT);
        } else {
            link = &wait->timer_next;
        }
    }
    MutexUnlock(&scheduler->lock);
}

static int
__get_next_deadline(
    _In_  struct usched_scheduler* scheduler,
    _Out_ struct timespec*         deadline)
{
    struct timespec     now;
    struct timespec     earliest;
    struct usched_wait* wait;
    bool                found = false;

    MutexLock(&scheduler->lock);
    for (wait = scheduler->timers; wait != NULL; wait = wait->timer_next) {
        if (!found || __is_before_or_equal(&wait->deadline, &earliest)) {
            earliest = wait->deadline;
            found = true;
        }
    }
    MutexUnlock(&scheduler->lock);
    if (!found) {
        errno = ENOENT;
        return -1;
    }

    if (deadline != NULL) {
        *deadline = earliest;
    }
    timespec_get(&now, TIME_UTC);
    errno = EWOULDBLOCK;
    return __is_before_or_equal(&earliest, &now) ? 0 : -1;
}

static void
__process_syscall_completions(
    _InOut_ struct usched_scheduler* scheduler)
{
    struct usched_syscall** link;
    struct usched_syscall*  syscall;

    MutexLock(&scheduler->lock);
    link = &scheduler->syscalls_pending;
    while (*link != NULL) {
        syscall = *link;

        // Use atomics for the error-code as the kernel will update this
        // using atomic operations.
        if (__atomic_load_n(&syscall->async_context->ErrorCode, __ATOMIC_ACQUIRE) != OS_ESCSTARTED) {
            *link = syscall->next;
            __job_ready_locked(syscall->job);
        } else {
            link = &syscall->next;
        }
    }
    MutexUnlock(&scheduler->lock);
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

    // Jobs must return to this point on the kernel thread's scheduler stack
    // before we can queue them again or free their stacks.
    if (setjmp(execution->jmp_context) != 0) {
        __tls_switch(execution->tls);
        __finish_dispatch(execution);
    }

    // Waits are kept on the scheduler, so this unit can stop even while
    // some of its jobs are still waiting.
    if (atomic_load(&execution->stop_requested)) {
        errno = ESHUTDOWN;
        return -1;
    }

    __update_timers(execution->scheduler);
    __process_syscall_completions(execution->scheduler);
    next = __get_next_ready(execution->scheduler);
    if (next == NULL) {
        return __get_next_deadline(execution->scheduler, deadline);
    }

    execution->current = next;
    __switch_task(next);
}

void usched_timedwait(const struct timespec* until)
{
    struct usched_execution_context* execution = __usched_get_execution();
    struct ioset_event               events[1];
    int                              numEvents;
    oserr_t                          oserr;
    
    TRACE("usched_timedwait()");

    // Ready jobs, new deadlines and syscall completions are all signalled
    // through the scheduler notification handle, so we only expect one event.
    oserr = OSNotificationQueueWait(
        &execution->notification_queue,
        &events[0],
        1,
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

void
usched_wait_async(void)
{
    struct usched_job*       current;
    struct usched_scheduler* scheduler;
    struct usched_syscall    syscall;

    current = __usched_get_execution()->current;
    scheduler = current->scheduler;
    
    syscall.job = current;
    syscall.async_context = &current->async_context;

    // Block the job and add the syscall under the same lock. The next scan
    // checks each pending syscall's error code, even if another unit has
    // already read its completion event.
    MutexLock(&scheduler->lock);
    
    assert(__JOB_STATE(current) == JobState_RUNNING);
    __JOB_SET_STATE(current, JobState_BLOCKING);
    
    syscall.next = scheduler->syscalls_pending;
    scheduler->syscalls_pending = &syscall;
    
    OSNotificationQueuePost(&scheduler->notification_handle, IOSETSYN);
    MutexUnlock(&scheduler->lock);
    usched_yield(NULL);
}

uuid_t
usched_notification_handle(void)
{
    return __usched_get_execution()->scheduler->notification_handle.ID;
}
