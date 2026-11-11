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
 * User Scheduler Definitions & Structures
 * - This header describes the base sched structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_USCHED_COND_H__
#define __OS_USCHED_COND_H__

// imported from time.h
struct timespec;

#include <os/spinlock.h>
#include <os/usched/mutex.h>

struct usched_job;
struct usched_wait;

struct usched_cnd {
    spinlock_t          lock;
    struct usched_wait* queue;
};

_CODE_BEGIN

/**
 * @brief Initializes a condition variable with an empty wait queue.
 *
 * @param condition The condition variable to initialize.
 */
CRTDECL(void, usched_cnd_init(struct usched_cnd* condition));

/**
 * @brief Blocks the current job until the condition variable is signalled.
 * The mutex must be held by the calling job. It is unlocked before the job
 * blocks and locked again before this function returns.
 *
 * @param condition The condition variable to wait for.
 * @param mutex     The mutex protecting the condition being checked.
 */
CRTDECL(void, usched_cnd_wait(struct usched_cnd* condition, struct usched_mtx* mutex));

/**
 * @brief Blocks the current job until the condition variable is signalled or
 * the deadline is reached. The mutex must be held by the calling job. It is
 * locked again before this function returns, including after a timeout.
 *
 * @param condition The condition variable to wait for.
 * @param mutex     The mutex to unlock before blocking.
 * @param until     Absolute deadline (TIME_UTC). NULL means no timeout.
 * @return          0 if signalled, or -1 with errno set to ETIME on timeout.
 */
CRTDECL(int, usched_cnd_timedwait(struct usched_cnd* condition, struct usched_mtx* mutex, const struct timespec *restrict));

/**
 * @brief Wakes one job waiting on the condition variable.
 *
 * @param condition The condition variable to signal.
 */
CRTDECL(void, usched_cnd_notify_one(struct usched_cnd* condition));

/**
 * @brief Wakes all jobs waiting on the condition variable.
 *
 * @param condition The condition variable to signal.
 */
CRTDECL(void, usched_cnd_notify_all(struct usched_cnd* condition));

_CODE_END
#endif //!__OS_USCHED_COND_H__
