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

#ifndef __OS_USCHED_RWLOCK_H__
#define __OS_USCHED_RWLOCK_H__

#include <os/usched/mutex.h>
#include <os/usched/cond.h>

struct usched_rwlock {
    struct usched_mtx sync_object;
    struct usched_cnd signal;
    int               readers;
};

/**
 * @brief Initializes a user-space reader-writer lock.
 * @param lock The lock to initialize.
 */
CRTDECL(void, usched_rwlock_init(struct usched_rwlock* lock));

/**
 * @brief Acquires a reader lock, waiting while a writer owns the lock.
 * @param lock The lock to acquire for reading.
 */
CRTDECL(void, usched_rwlock_r_lock(struct usched_rwlock* lock));

/**
 * @brief Releases a reader lock.
 * @param lock The lock to release.
 */
CRTDECL(void, usched_rwlock_r_unlock(struct usched_rwlock* lock));

/**
 * @brief Promotes the current reader lock to a writer lock.
 * @param lock The lock to promote.
 */
CRTDECL(void, usched_rwlock_w_promote(struct usched_rwlock* lock));

/**
 * @brief Demotes the current writer lock to a reader lock.
 * @param lock The lock to demote.
 */
CRTDECL(void, usched_rwlock_w_demote(struct usched_rwlock* lock));

/**
 * @brief Acquires a writer lock, waiting while readers or another writer hold it.
 * @param lock The lock to acquire for writing.
 */
CRTDECL(void, usched_rwlock_w_lock(struct usched_rwlock* lock));

/**
 * @brief Releases a writer lock.
 * @param lock The lock to release.
 */
CRTDECL(void, usched_rwlock_w_unlock(struct usched_rwlock* lock));

#endif //!__OS_USCHED_RWLOCK_H__
