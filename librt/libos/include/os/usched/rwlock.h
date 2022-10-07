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
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_init(struct usched_rwlock* lock));

/**
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_r_lock(struct usched_rwlock* lock));

/**
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_r_unlock(struct usched_rwlock* lock));

/**
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_w_promote(struct usched_rwlock* lock));

/**
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_w_demote(struct usched_rwlock* lock));

/**
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_w_lock(struct usched_rwlock* lock));

/**
 * @brief
 * @param lock
 */
CRTDECL(void, usched_rwlock_w_unlock(struct usched_rwlock* lock));

#endif //!__OS_USCHED_RWLOCK_H__
