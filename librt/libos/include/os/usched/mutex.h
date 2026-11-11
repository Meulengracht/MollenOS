/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __OS_USCHED_MUTEX_H__
#define __OS_USCHED_MUTEX_H__

// imported from time.h
struct timespec;

#include <os/spinlock.h>

struct usched_job;
struct usched_wait;

enum {
    USCHED_MUTEX_PLAIN       = 0,
    USCHED_MUTEX_RECURSIVE   = 1,
    USCHED_MUTEX_TIMED       = 2
};

/**
 * @brief Tracks the job holding the mutex, its recursive lock count and waiting
 * jobs. The internal lock protects owner, references and queue.
 */
struct usched_mtx {
    int                 type;
    int                 references;
    spinlock_t          lock;
    struct usched_job*  owner;
    struct usched_wait* queue;
};

_CODE_BEGIN

/**
 * @brief Initializes an unlocked mutex.
 *
 * @param mutex The mutex to initialize.
 * @param type  USCHED_MUTEX_* flags.
 */
CRTDECL(void, usched_mtx_init(struct usched_mtx* mutex, int type));

/**
 * @brief Locks the mutex, blocking the current job if necessary until the
 * mutex is available or the deadline is reached.
 *
 * @param mutex The mutex to lock.
 * @param until Absolute deadline (TIME_UTC). NULL means no timeout.
 * @return      0 if locked, or -1 with errno set to ETIME on timeout.
 */
CRTDECL(int, usched_mtx_timedlock(struct usched_mtx* mutex, const struct timespec *restrict until));

/**
 * @brief Tries to lock the mutex without blocking.
 *
 * @param mutex The mutex to lock.
 * @return      0 if locked, or -1 with errno set to EBUSY if unavailable.
 */
CRTDECL(int, usched_mtx_trylock(struct usched_mtx* mutex));

/**
 * @brief Locks the mutex, blocking the current job until it is available.
 *
 * @param mutex The mutex to lock.
 */
CRTDECL(void, usched_mtx_lock(struct usched_mtx* mutex));

/**
 * @brief Unlocks a mutex held by the calling job. A recursive mutex must be
 * unlocked once for each successful lock before another job can take it.
 *
 * @param mutex The mutex to unlock.
 */
CRTDECL(void, usched_mtx_unlock(struct usched_mtx* mutex));

_CODE_END
#endif //!__OS_USCHED_MUTEX_H__
