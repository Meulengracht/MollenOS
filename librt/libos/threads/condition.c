/**
 * MollenOS
 *
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

#include <errno.h>
#include <os/futex.h>
#include <os/condition.h>
#include <time.h>

oserr_t
ConditionInitialize(
        _In_ Condition_t* cond)
{
    if (!cond) {
        return OS_EINVALPARAMS;
    }

    atomic_store(&cond->Value, 0);
    return OS_EOK;
}

void
ConditionDestroy(
        _In_ Condition_t* cond)
{
	if (!cond) {
		return;
	}
    ConditionBroadcast(cond);
}

oserr_t
ConditionSignal(
        _In_ Condition_t* cond)
{
    OSFutexParameters_t parameters;
    
	if (cond == NULL) {
		return OS_EINVALPARAMS;
	}

    parameters.Futex0    = &cond->Value;
    parameters.Expected0 = 1;
    parameters.Flags     = FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE;
	return OSFutex(&parameters, NULL);
}

oserr_t
ConditionBroadcast(
        _In_ Condition_t* cond)
{
    OSFutexParameters_t parameters;
    
	if (cond == NULL) {
        return OS_EINVALPARAMS;
	}
	
    parameters.Futex0    = &cond->Value;
    parameters.Expected0 = atomic_load(&cond->Value);
    parameters.Flags     = FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE;
	return OSFutex(&parameters, NULL);
}

oserr_t
ConditionWait(
        _In_ Condition_t*      cond,
        _In_ Mutex_t*          mutex,
        _In_ OSAsyncContext_t* asyncContext)
{
    OSFutexParameters_t parameters;
    oserr_t             oserr;
	if (cond == NULL || mutex == NULL) {
		return OS_EINVALPARAMS;
	}

    parameters.Futex0    = &cond->Value;
    parameters.Futex1    = &mutex->Value;
    parameters.Expected0 = atomic_load(&cond->Value);
    parameters.Count     = 1; // Wakeup one on the mutex
    parameters.Op        = FUTEX_OP(FUTEX_OP_SET, 0, 0, 0);
    parameters.Flags     = FUTEX_FLAG_WAIT | FUTEX_FLAG_PRIVATE | FUTEX_FLAG_OP;
    parameters.Deadline  = NULL;

    oserr = OSFutex(&parameters, asyncContext);
    (void)MutexLock(mutex);
    return oserr;
}

oserr_t
ConditionTimedWait(
        _In_ Condition_t* restrict           cond,
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec* restrict timePoint,
        _In_ OSAsyncContext_t*               asyncContext)
{
    OSFutexParameters_t parameters;
	oserr_t             status;

	if (cond == NULL || mutex == NULL || timePoint == NULL) {
		return OS_EINVALPARAMS;
	}

    parameters.Futex0    = &cond->Value;
    parameters.Futex1    = &mutex->Value;
    parameters.Expected0 = atomic_load(&cond->Value);
    parameters.Count     = 1; // Wakeup one on the mutex
    parameters.Op        = FUTEX_OP(FUTEX_OP_SET, 0, 0, 0); // Reset mutex to 0
    parameters.Flags     = FUTEX_FLAG_WAIT | FUTEX_FLAG_PRIVATE | FUTEX_FLAG_OP;
    parameters.Deadline  = timePoint == NULL ? NULL : &(OSTimestamp_t) {
        .Seconds = timePoint->tv_sec,
        .Nanoseconds = timePoint->tv_nsec
    };
    
    status = OSFutex(&parameters, asyncContext);
    MutexLock(mutex);
    return status;
}
