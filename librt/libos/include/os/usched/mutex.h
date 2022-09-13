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

#ifndef __OS_USCHED_MUTEX_H__
#define __OS_USCHED_MUTEX_H__

// imported from time.h
struct timespec;

#include <os/spinlock.h>

struct usched_job;

enum {
    USCHED_MUTEX_PLAIN       = 0,
    USCHED_MUTEX_RECURSIVE   = 1,
    USCHED_MUTEX_TIMED       = 2
};

struct usched_mtx {
    int                type;
    spinlock_t         lock;
    struct usched_job* owner;
    struct usched_job* queue;
};

/**
 * @brief Initializes the mutex provided to its default state.
 *
 * @param mutex A pointer to a mutex that should be initialized.
 */
CRTDECL(void, usched_mtx_init(struct usched_mtx* mutex));

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is
 * locked or until the TIME_UTC based time point pointed to by until has been reached.
 * @param mutex
 * @param time_point
 * @return
 */
CRTDECL(int, usched_mtx_timedlock(struct usched_mtx* mutex, const struct timespec *restrict until));

/**
 * @brief Tries to lock the mutex pointed to by mutex without blocking.
 * @param mutex
 * @return
 */
CRTDECL(int, usched_mtx_trylock(struct usched_mtx* mutex));

/**
 * @brief Locks a mutex, and if the mutex is already locked then this function will block.
 *
 * @param mutex The mutex that should be locked.
 */
CRTDECL(void, usched_mtx_lock(struct usched_mtx* mutex));

/**
 * @brief Unlocks an already held mutex.
 *
 * @param mutex The mutex that should be unlocked.
 */
CRTDECL(void, usched_mtx_unlock(struct usched_mtx* mutex));

#endif //!__OS_USCHED_MUTEX_H__
