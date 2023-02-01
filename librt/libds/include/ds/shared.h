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

#ifndef __LIBDS_H__
#define __LIBDS_H__

#if defined(VALI)
#ifdef __LIBDS_KERNEL__
#include <spinlock.h>
typedef Spinlock_t syncobject_t;
#define SYNC_INIT                OS_SPINLOCK_INIT
#define SYNC_INIT_FN(collection) SpinlockConstruct(&(collection)->lock)
#define SYNC_LOCK(collection)    SpinlockAcquireIrq(&(collection)->lock)
#define SYNC_UNLOCK(collection)  SpinlockReleaseIrq(&(collection)->lock)
#else
#include <os/spinlock.h>
typedef spinlock_t syncobject_t;
#define SYNC_INIT                _SPN_INITIALIZER_NP
#define SYNC_INIT_FN(collection) spinlock_init(&(collection)->lock)
#define SYNC_LOCK(collection)    spinlock_acquire(&(collection)->lock)
#define SYNC_UNLOCK(collection)  spinlock_release(&(collection)->lock)
#endif

#else
// Host build used for unit test
#include <stdatomic.h>
typedef struct spinlock {
    _Atomic(int) locked;
} syncobject_t;

static inline void spinlock_lock(struct spinlock* spinlock) {
    int zero = 0;
    while (!atomic_compare_exchange_weak(&spinlock->locked, &zero, 1)) {
    }
}
static inline void spinlock_unlock(struct spinlock* spinlock) {
    atomic_store(&spinlock->locked, 0);
}

#define SYNC_INIT { 0 };
#define SYNC_INIT_FN(collection) (collection)->lock.locked = 0
#define SYNC_LOCK(collection)    spinlock_lock(&(collection)->lock)
#define SYNC_UNLOCK(collection)  spinlock_unlock(&(collection)->lock)
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

typedef struct element {
    struct element* next;
    struct element* previous;
    
    void* key;
    void* value;
} element_t;

#define ELEMENT_INIT(elem, _key, _value) (elem)->next = NULL; (elem)->previous = NULL; (elem)->key = (void*)(_key); (elem)->value = (void*)_value

// These are for unlocked access and destroy the purpose of synchronization
// if used out of library.
#define foreach(i, collection)           for (element_t* (i) = (collection)->head; (i) != NULL; (i) = (i)->next)
#define foreach_reverse(i, collection)   for (element_t* (i) = (collection)->tail; (i) != NULL; (i) = (i)->previous)
#define foreach_volatile(i, collection)  for (element_t* (i) = READ_VOLATILE((collection)->head); (i) != NULL; (i) = (i)->next)
#define _foreach(i, collection)          for ((i) = (collection)->head; (i) != NULL; (i) = (i)->next)
#define _foreach_reverse(i, collection)  for ((i) = (collection)->tail; (i) != NULL; (i) = (i)->previous)
#define _foreach_volatile(i, collection) for ((i) = READ_VOLATILE((collection)->head); (i) != NULL; (i) = (i)->next)

#define foreach_nolink(i, collection)  for (element_t* (i) = (collection)->head; (i) != NULL; )
#define _foreach_nolink(i, collection) for ((i) = (collection)->head; (i) != NULL; )

#endif //!__LIBDS_H__
