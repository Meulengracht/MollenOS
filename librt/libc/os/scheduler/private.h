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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * User threads implementation. Implements support for multiple tasks running entirely
 * in userspace. This is supported by additional synchronization primitives in the usched_
 * namespace.
 */

#ifndef __USCHED_PRIVATE_H__
#define __USCHED_PRIVATE_H__

#include <threads.h>

#define SCHEDULER_MAGIC 0xDEADB00B

enum JobState {
    JobState_CREATED,
    JobState_RUNNING,
    JobState_BLOCKED,
    JobState_FINISHING
};

struct usched_job {
    void*         stack;
    unsigned int  stack_size;
    jmp_buf       context;
    enum JobState  state;
    usched_task_fn entry;
    void*         argument;
    int           cancelled;

    struct usched_job* next;
};

#define SHOULD_RESCHEDULE(job) ((job)->state == JobState_CREATED || (job)->state == JobState_RUNNING)

struct usched_scheduler {
    int     magic;
    mtx_t   lock;
    jmp_buf context;

    struct usched_job* current;
    struct usched_job* ready;
    struct usched_job* garbage_bin;
};

static inline void AppendJob(struct usched_job** list, struct usched_job* job)
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

extern struct usched_scheduler* __usched_get_scheduler(void);

#endif //!__USCHED_PRIVATE_H__
