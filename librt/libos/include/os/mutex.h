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

#ifndef __OS_MUTEX_H__
#define __OS_MUTEX_H__

// imported from time.h
struct timespec;

#include <os/osdefs.h>

enum {
    MUTEX_PLAIN       = 0,
    MUTEX_RECURSIVE   = 1,
    MUTEX_TIMED       = 2
};

typedef struct Mutex {
    int          Flags;
    uuid_t       Owner;
    _Atomic(int) References;
    _Atomic(int) Value;
} Mutex_t;
// _MTX_INITIALIZER_NP

#if defined(__cplusplus)
#define MUTEX_INIT(type) { type, UUID_INVALID, 0, 0 }
#else
#define MUTEX_INIT(type) { type, UUID_INVALID, ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0) }
#endif

_CODE_BEGIN
/**
 * @brief Initializes a new mutex object
 * @param mutex
 * @param flags
 * @return
 */
CRTDECL(oserr_t,
MutexInitialize(
        _In_ Mutex_t* mutex,
        _In_ int      flags));

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is locked.
 * The behavior is undefined if the current thread has already locked the mutex
 * and the mutex is not recursive.
 * @param mutex
 * @return
 */
CRTDECL(oserr_t,
MutexLock(
        _In_ Mutex_t* mutex));

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is
 * locked or until the TIME_UTC based time point pointed to by timePoint has been reached.
 * @param mutex
 * @param timePoint
 * @return
 */
CRTDECL(oserr_t,
MutexTimedLock(
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec *restrict timePoint));

/**
 * @brief Tries to lock the mutex pointed to by mutex without blocking.
 * @param mutex
 * @return
 */
CRTDECL(oserr_t,
MutexTryLock(
        _In_ Mutex_t* mutex));

/**
 * @brief Unlocks the mutex pointed to by mutex.
 * @param mutex
 * @return
 */
CRTDECL(oserr_t,
MutexUnlock(
        _In_ Mutex_t* mutex));

/**
 * @brief Destroys the mutex pointed to by mutex. If there are threads waiting on mutex,
 * the behavior is undefined.
 * @param mutex
 */
CRTDECL(void,
MutexDestroy(
        _In_ Mutex_t* mutex));

_CODE_END
#endif //!__OS_MUTEX_H__
