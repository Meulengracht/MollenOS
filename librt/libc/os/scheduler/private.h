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
 * User threads implementation. Implements support for multiple tasks running entirely
 * in userspace. This is supported by additional synchronization primitives in the usched_
 * namespace.
 */

#ifndef __USCHED_PRIVATE_H__
#define __USCHED_PRIVATE_H__

#include <internal/_tls.h>
#include <threads.h>

#define SCHEDULER_MAGIC 0xDEADB00B

enum JobState {
    JobState_CREATED,
    JobState_RUNNING,
    JobState_BLOCKED,
    JobState_FINISHING
};

struct usched_job {
    void*                 stack;
    unsigned int          stack_size;
    jmp_buf               context;
    enum JobState         state;
    usched_task_fn        entry;
    void*                 argument;
    int                   cancelled;
    int                   weight;
    struct thread_storage tls;

    struct usched_job* next;
};

struct usched_job_queue {
    struct usched_job* next;
    mtx_t              lock;
};

#define SHOULD_RESCHEDULE(job) ((job)->state == JobState_CREATED || (job)->state == JobState_RUNNING)

struct usched_timeout {
    int                    id;
    clock_t                deadline;
    int                    active;
    struct usched_job*     job;
    struct usched_cnd*     signal;
    struct usched_timeout* next;
};

struct usched_scheduler {
    int                    magic;
    mtx_t                  lock;
    jmp_buf                context;
    struct thread_storage* tls;

    struct usched_job* current;
    struct usched_job* ready;
    struct usched_job* garbage_bin;

    struct usched_timeout* timers;
};

struct execution_unit_tls {
    struct usched_scheduler* scheduler;
    struct usched_job_queue  wait_queue;
};

struct usched_execution_unit {
    uuid_t                        thread_id;
    unsigned int                  load;
    atomic_int                    sync;
    unsigned int                  locked;
    struct usched_scheduler       scheduler;
    struct execution_unit_tls     tls;
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

extern int __usched_xunit_queue_job(struct usched_job* job, struct usched_job_parameters* params);

extern void                       __usched_init(struct usched_scheduler* sched);
extern void                       __usched_destroy(struct usched_scheduler* sched);
extern int                        __usched_prepare_migrate(void);
extern struct usched_scheduler*   __usched_get_scheduler(void);
extern int                        __usched_timeout_start(unsigned int timeout, struct usched_cnd* cond);
extern int                        __usched_timeout_finish(int id);
extern void                       __usched_cond_notify_job(struct usched_cnd* cond, struct usched_job* job);
extern struct execution_unit_tls* __usched_xunit_tls_current(void);

#endif //!__USCHED_PRIVATE_H__
