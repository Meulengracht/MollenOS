/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Mutex Support Definitions & Structures
 * - This header describes the base mutex-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __MUTEX_H__
#define __MUTEX_H__

#include <os/spinlock.h>
#include <os/osdefs.h>

enum {
    MutexSimple    = 0,
    MutexRecursive = 1
};

enum {
    MutexSuccess    = 0,
    MutexBusy       = 1,
    MutexError      = -1
};

typedef struct {
    int          _flags;
    UUId_t       _owner;
    _Atomic(int) _count;
    Spinlock_t   _syncobject;
} Mutex_t;

#define MUTEX_INIT(Type)    { Type, UUID_INVALID, 0, SPINLOCK_INIT(0) }

/* MutexInitialize
 * Creates a new mutex object with type. The object pointed to by mutex is set to an 
 * identifier of the newly created mutex. */
CRTDECL(int,
MutexInitialize(
    _In_ Mutex_t* Mutex,
    _In_ int      Type));

/* MutexAcquire 
 * Blocks the current thread until the mutex pointed to by mutex is locked.
 * The behavior is undefined if the current thread has already locked the mutex 
 * and the mutex is not recursive. */
CRTDECL(int,
MutexAcquire(
    _In_ Mutex_t* Mutex));

/* MutexTryAcquire
 * Tries to lock the mutex pointed to by mutex without blocking. 
 * Returns immediately if the mutex is already locked. */
CRTDECL(int,
MutexTryAcquire(
    _In_ Mutex_t* Mutex));

/* MutexRelease
 * Unlocks the mutex pointed to by mutex. */
CRTDECL(int,
MutexRelease(
    _In_ Mutex_t* Mutex));

/* MutexDestroy
 * Destroys the mutex pointed to by mutex. If there are threads waiting on mutex, 
 * the behavior is undefined. */
CRTDECL(void,
MutexDestroy(
    _In_ Mutex_t* Mutex));

#endif //!__MUTEX_H__
