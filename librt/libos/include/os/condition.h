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

#ifndef __OS_CONDITION_H__
#define __OS_CONDITION_H__

// imported from time.h
struct timespec;

#include <os/osdefs.h>
#include <os/types/async.h>
#include <os/mutex.h>

typedef struct Condition {
    _Atomic(int) Value;
} Condition_t;

#if defined(__cplusplus)
#define COND_INIT           { 0 }
#else
#define COND_INIT           { ATOMIC_VAR_INIT(0) }
#endif

_CODE_BEGIN
/**
 * @brief Initializes a new condition variable.
 * @param cond
 * @return
 */
CRTDECL(oserr_t,
ConditionInitialize(
        _In_ Condition_t* cond));

/**
 * @brief Unblocks one thread that currently waits on condition variable pointed to by cond.
 * @param cond
 * @return
 */
CRTDECL(oserr_t,
ConditionSignal(
        _In_ Condition_t* cond));

/**
 * @brief Unblocks all thread that currently wait on condition variable pointed to by cond.
 * @param cond
 * @return
 */
CRTDECL(oserr_t,
ConditionBroadcast(
        _In_ Condition_t* cond));

/**
 * @brief Atomically unlocks the mutex pointed to by mutex and blocks on the
 * condition variable pointed to by cond until the thread is signalled
 * by ConditionSignal or ConditionBroadcast.
 * The mutex is locked again before the function returns.
 * @param cond
 * @param mutex
 * @return
 */
CRTDECL(oserr_t,
ConditionWait(
        _In_ Condition_t*        cond,
        _In_ Mutex_t*            mutex,
        _In_ OSAsyncContext_t* asyncContext));

/**
 * @brief Atomically unlocks the mutex pointed to by mutex and blocks on the
 * condition variable pointed to by cond until the thread is signalled
 * by ConditionSignal or ConditionBroadcast, or until the TIME_UTC based time point
 * pointed to by timePoint has been reached. The mutex is locked again
 * before the function returns.
 * @param cond
 * @param mutex
 * @param timePoint
 * @return
 */
CRTDECL(oserr_t,
ConditionTimedWait(
        _In_ Condition_t* restrict           cond,
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec* restrict timePoint,
        _In_ OSAsyncContext_t*             asyncContext));

/**
 * @brief Destroys the condition variable pointed to by cond. If there are threads
 * waiting on cond, the behavior is undefined.
 * @param cond
 */
CRTDECL(void,
ConditionDestroy(
        _In_ Condition_t* cond));

_CODE_END
#endif //!__OS_CONDITION_H__
