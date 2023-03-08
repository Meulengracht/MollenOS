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

#ifndef __USCHED_PRIVATE_H__
#define __USCHED_PRIVATE_H__

#include <ds/hashtable.h>
#include <internal/_tls.h>
#include <os/mutex.h>
#include <os/threads.h>
#include <os/types/async.h>
#include <os/usched/types.h>
#include <os/usched/cond.h>
#include <os/usched/job.h>
#include <setjmp.h>
#include <time.h>

#define SCHEDULER_MAGIC 0xDEADB00B

enum job_state {
    // Current state
    JobState_CREATED = 0,
    JobState_RUNNING = 1,
    JobState_BLOCKED = 2,
    JobState_FINISHING = 3,

    // State flags
    JobState_CANCELLED = 0x10
};

struct usched_scheduler_queue;

struct usched_job {
    uuid_t         id;
    void*          stack;
    unsigned int   stack_size;
    bool           detached;
    jmp_buf        context;
    enum job_state state;

    // async_context is the job specific async context
    // which is used implicitly by the libc functions.
    OSAsyncContext_t async_context;

    // entry is the job entry function.
    usched_task_fn entry;

    // argument is the job argument to use when invoking
    // the job entry.
    void* argument;

    // tls is the (libc) thread local storage for this job. This
    // is located as a part of the job pointer as that is as good
    // a place as any.
    struct thread_storage tls;

    // queue describes which queue the job belongs to. The job can
    // either belong to the global queue, or it belongs to a specific
    // scheduler (i.e being detached). We need to know this for when
    // requeuing the job after being blocked or sleep.
    struct usched_scheduler_queue* queue;

    // next is the pointer to the next job for the queues it is in.
    // Since a job can only be in one queue at the time, the link pointer
    // is kept as a part of the job.
    struct usched_job* next;
};

#define SHOULD_RESCHEDULE(job) ((job)->state == JobState_CREATED || (job)->state == JobState_RUNNING)

#define __QUEUE_TYPE_SLEEP 0
#define __QUEUE_TYPE_COND  1
#define __QUEUE_TYPE_MUTEX 2

union usched_timer_queue {
    struct usched_cnd* cond;
    struct usched_mtx* mutex;
};

struct usched_timeout {
    int                      id;
    struct timespec          deadline;
    int                      active;
    struct usched_job*       job;
    union usched_timer_queue queue;
    int                      queue_type;
    struct usched_timeout*   next;
};

struct usched_syscall {
    struct usched_job*     job;
    OSAsyncContext_t*      async_context;
    struct usched_syscall* next;
};

struct usched_scheduler {
    int                    magic;
    jmp_buf                context;
    struct thread_storage* tls;
    OSHandle_t             notification_queue;
    OSHandle_t             syscall_handle;

    // internal_queue is the queue that is only specific to this scheduler.
    // If this is non-NULL, then the scheduler is using a seperate queue for jobs
    // and not the global queue.
    struct usched_scheduler_queue* internal_queue;
    struct usched_syscall*         syscalls_pending;

    struct usched_job* current;
    struct usched_job* garbage_bin;

    struct usched_timeout* timers;
};

struct execution_unit_tls {
    struct usched_scheduler* scheduler;
};

struct execution_unit_params {
    struct usched_job* detached_job;
};

struct usched_execution_unit {
    uuid_t                        thread_id;
    struct usched_scheduler       scheduler;
    struct execution_unit_tls     tls;
    struct execution_unit_params  params;
    struct usched_execution_unit* next;
};

struct job_entry_context {
    struct usched_mtx   mtx;
    struct usched_cnd   cond;
    int                 exit_code;
    struct usched_job*  job;
};

struct job_entry {
    uuid_t                    id;
    struct job_entry_context* context;
};

struct execution_manager {
    struct usched_execution_unit  primary;
    struct usched_execution_unit* detached;
    int                           count;
    Mutex_t                       lock;
    int                           core_count;

    hashtable_t                   jobs;
    uuid_t                        jobs_id;
    Mutex_t                       jobs_lock;
};

#define DEFINE_LIST_APPEND(type, name) \
    static inline void __usched_ ## append_ ## name(type** list, type* item) { \
        type* i = *list;               \
        if (!i) {                      \
            *list = item;              \
            return;                    \
        }                              \
        while (i->next) {              \
            i = i->next;               \
        }                              \
        i->next = item;                \
    }

#define DEFINE_LIST_REMOVE(type, name) \
    static inline void __usched_ ## remove_ ## name(type** list, type* item) { \
        type* i = *list;               \
        type* p = NULL;                \
        while (i != NULL) {            \
            if (i == item) {           \
                if (p == NULL) {       \
                    *list = i->next;   \
                } else {               \
                    p->next = i->next; \
                }                      \
                return;                \
            }                          \
            i = i->next;               \
        }                              \
    }

DEFINE_LIST_APPEND(struct usched_timeout, timer)
DEFINE_LIST_APPEND(struct usched_job, job)
DEFINE_LIST_APPEND(struct usched_execution_unit, xunit)
DEFINE_LIST_REMOVE(struct usched_execution_unit, xunit)

struct usched_init_params {
    struct usched_job* detached_job;
};

/**
 * @brief Should run once during XU startup. This initializes a few resources
 * that only need to run once.
 */
extern void __usched_startup(void);

/**
 * @brief Initializes the usched scheduler for the current execution unit. Only
 * called once per execution unit.
 * @param sched
 * @param params
 */
extern void __usched_init(struct usched_scheduler* sched, struct usched_init_params* params);

/**
 * @brief Destroys any resources allocated by __usched_init
 * @param sched
 */
extern void __usched_destroy(struct usched_scheduler* sched);

extern int __xunit_start_detached(struct usched_job* job, struct usched_job_parameters* params);

extern struct execution_manager*  __xunit_manager(void);
extern void                       __usched_add_job_ready(struct usched_job* job);
extern int                        __usched_prepare_migrate(void);
extern struct usched_scheduler*   __usched_get_scheduler(void);
extern struct execution_unit_tls* __usched_xunit_tls_current(void);
extern bool                       __usched_job_has_exit(uuid_t jobID);

extern int                        __usched_timeout_start(const struct timespec *restrict until, union usched_timer_queue* queue, int queueType);
extern int                        __usched_timeout_finish(int id);
extern void                       __usched_job_notify(struct usched_job* job);
extern void                       __usched_cond_notify_job(struct usched_cnd* cond, struct usched_job* job);
extern void                       __usched_mtx_notify_job(struct usched_mtx* mtx, struct usched_job* job);

#endif //!__USCHED_PRIVATE_H__
