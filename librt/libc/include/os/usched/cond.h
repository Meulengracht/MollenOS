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
 * User Scheduler Definitions & Structures
 * - This header describes the base sched structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_USCHED_COND_H__
#define __OS_USCHED_COND_H__

#include <os/spinlock.h>

struct usched_job;
struct usched_mtx;

struct usched_cnd {
    struct usched_job* queue;
};

/**
 * @brief Initializes a new condition variable instance.
 *
 * @param condition A pointer to a condition variable that should be initialized
 */
CRTDECL(void, usched_cnd_init(struct usched_cnd* condition));

/**
 * @brief Blocks the current running task until the condition variable has been signalled.
 *
 * @param condition The condition variable that should be waited for.
 * @param mutex     A mutex protecting the condition variable, this will be unlocked before blocking.
 */
CRTDECL(void, usched_cnd_wait(struct usched_cnd* condition, struct usched_mtx* mutex));

/**
 * @brief Notifies one task that is currently blocked by the condition variable, and wakes it up.
 *
 * @param condition The condition variable that should be signalled.
 */
CRTDECL(void, usched_cnd_notify_one(struct usched_cnd* condition));

/**
 * @brief Notifies all waiters that are currently blocked by a condition variable, and wakes them up.
 *
 * @param condition The condition variable that should be signalled.
 */
CRTDECL(void, usched_cnd_notify_all(struct usched_cnd* condition));

#endif //!__OS_USCHED_COND_H__
