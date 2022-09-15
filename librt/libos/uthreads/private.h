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

#include <internal/_tls.h>
#include <os/usched/types.h>
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

};

struct usched_job {
    uuid_t                id;
    void*                 stack;
    unsigned int          stack_size;
    jmp_buf               context;
    enum job_state        state;
    usched_task_fn        entry;
    void*                 argument;
    struct thread_storage tls;

    struct usched_job* next;
};

#define SHOULD_RESCHEDULE(job) ((job)->state == JobState_CREATED || (job)->state == JobState_RUNNING)

#define __QUEUE_TYPE_COND  1
#define __QUEUE_TYPE_MUTEX 2

struct usched_timeout {
    int                    id;
    struct timespec        deadline;
    int                    active;
    struct usched_job*     job;
    union {
        struct usched_cnd* cond;
        struct usched_mtx* mutex;
    } queue;
    int                    queue_type;
    struct usched_timeout* next;
};

struct usched_scheduler {
    int                    magic;
    jmp_buf                context;
    struct thread_storage* tls;
    bool                   detached;

    struct usched_job* current;
    struct usched_job* internal_queue;
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

static inline void __usched_append_timer(struct usched_timeout** list, struct usched_timeout* timer)
{
    struct usched_timeout* i = *list;
    if (!i) {
        *list = timer;
        return;
    }

    while (i->next) {
        i = i->next;
    }
    i->next = timer;
}

static inline void __usched_append_job(struct usched_job** list, struct usched_job* job)
{
    struct usched_job* i = *list;
    if (!i) {
        *list = job;
        return;
    }

    while (i->next) {
        i = i->next;
    }
    i->next = job;
}

extern int __xunit_start_detached(struct usched_job* job, struct usched_job_parameters* params);

struct usched_init_params {
    struct usched_job* detached_job;
};

/**
 * @brief Initializes the usched scheduler for the current execution unit. Only
 * called once per execution unit.
 * @param sched
 * @param params
 */
extern void __usched_init(struct usched_scheduler* sched, struct usched_init_params* params);


extern void                       __usched_add_job_ready(struct usched_job* job);
extern int                        __usched_prepare_migrate(void);
extern struct usched_scheduler*   __usched_get_scheduler(void);
extern struct execution_unit_tls* __usched_xunit_tls_current(void);

extern int                        __usched_timeout_start_cond(const struct timespec *restrict until, struct usched_cnd* cond);
extern int                        __usched_timeout_start_mtx(const struct timespec *restrict until, struct usched_mtx* mtx);
extern int                        __usched_timeout_finish(int id);
extern void                       __usched_cond_notify_job(struct usched_cnd* cond, struct usched_job* job);
extern void                       __usched_mtx_notify_job(struct usched_mtx* mtx, struct usched_job* job);

#endif //!__USCHED_PRIVATE_H__
