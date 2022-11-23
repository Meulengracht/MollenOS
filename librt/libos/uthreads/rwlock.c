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

#include <os/usched/rwlock.h>

void usched_rwlock_init(struct usched_rwlock* lock)
{
    usched_mtx_init(&lock->sync_object, USCHED_MUTEX_PLAIN);
    usched_cnd_init(&lock->signal);
    lock->readers = 0;
}

void usched_rwlock_r_lock(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    lock->readers++;
    usched_mtx_unlock(&lock->sync_object);
}

void usched_rwlock_r_unlock(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    lock->readers--;
    if (!lock->readers) {
        usched_cnd_notify_one(&lock->signal);
    }
    usched_mtx_unlock(&lock->sync_object);
}

void usched_rwlock_w_promote(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    if (--(lock->readers)) {
        usched_cnd_wait(&lock->signal, &lock->sync_object);
    }
}

void usched_rwlock_w_demote(struct usched_rwlock* lock)
{
    lock->readers++;
    usched_mtx_unlock(&lock->sync_object);
}

void usched_rwlock_w_lock(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    if (lock->readers) {
        usched_cnd_wait(&lock->signal, &lock->sync_object);
    }
}

void usched_rwlock_w_unlock(struct usched_rwlock* lock)
{
    usched_mtx_unlock(&lock->sync_object);
    usched_cnd_notify_one(&lock->signal);
}
