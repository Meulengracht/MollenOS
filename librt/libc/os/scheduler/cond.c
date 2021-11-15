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

#include <os/usched/usched.h>
#include <os/usched/mutex.h>
#include <os/usched/cond.h>
#include <setjmp.h>
#include <assert.h>
#include "private.h"

void usched_cnd_init(struct usched_cnd* condition)
{
    assert(condition != NULL);
    usched_mtx_init(&condition->lock);
    condition->queue = NULL;
}

void usched_cnd_wait(struct usched_cnd* condition, struct usched_mtx* mutex)
{
    struct usched_job* current;
    assert(condition != NULL);
    assert(mutex != NULL);

    usched_mtx_lock(&condition->lock);
    current = __usched_get_scheduler()->current;
    current->state = JobState_BLOCKED;
    AppendJob(&condition->queue, current);
    usched_mtx_unlock(&condition->lock);

    usched_mtx_unlock(mutex);
    usched_yield();
    usched_mtx_lock(mutex);
}

int usched_cnd_wait_timed(struct usched_cnd* condition, struct usched_mtx* mutex, unsigned int timeout)
{
    struct usched_job* current;
    int                timer;
    int                status;
    assert(condition != NULL);
    assert(mutex != NULL);

    usched_mtx_lock(&condition->lock);
    current = __usched_get_scheduler()->current;
    current->state = JobState_BLOCKED;
    AppendJob(&condition->queue, current);
    usched_mtx_unlock(&condition->lock);

    usched_mtx_unlock(mutex);
    timer = __usched_timeout_start(timeout, condition);
    usched_yield();
    status = __usched_timeout_finish(timer);
    usched_mtx_lock(mutex);
    return status;
}

void usched_cnd_notify_one(struct usched_cnd* condition)
{
    assert(condition != NULL);

    usched_mtx_lock(&condition->lock);
    if (condition->queue) {
        struct usched_job* job = condition->queue;
        condition->queue = job->next;
        job->next = NULL;

        job->state = JobState_RUNNING;
        AppendJob(&__usched_get_scheduler()->ready, job);
    }
    usched_mtx_unlock(&condition->lock);
}

void usched_cnd_notify_all(struct usched_cnd* condition)
{
    assert(condition != NULL);

    usched_mtx_lock(&condition->lock);
    while (condition->queue) {
        struct usched_job* job = condition->queue;
        condition->queue = job->next;
        job->next = NULL;

        job->state = JobState_RUNNING;
        AppendJob(&__usched_get_scheduler()->ready, job);
    }
    usched_mtx_unlock(&condition->lock);
}

void __usched_cond_notify_job(struct usched_cnd* condition, struct usched_job* job)
{
    assert(condition != NULL);
    assert(job != NULL);

    usched_mtx_lock(&condition->lock);
    if (condition->queue) {
        struct usched_job* i = condition->queue, *previous = NULL;
        while (i) {
            if (i == job) {
                if (!previous) {
                    condition->queue = i->next;
                }
                else {
                    previous->next = i->next;
                }

                job->next = NULL;
                job->state = JobState_RUNNING;
                AppendJob(&__usched_get_scheduler()->ready, job);
            }

            previous = i;
            i = i->next;
        }
    }
    usched_mtx_unlock(&condition->lock);
}
