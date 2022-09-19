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
        return OsInvalidParameters;
    }

    atomic_store(&cond->Value, 0);
    return OsOK;
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
    FutexParameters_t parameters;
    
	if (cond == NULL) {
		return OsInvalidParameters;
	}
	
    parameters._futex0  = &cond->Value;
    parameters._val0    = 1;
    parameters._flags   = FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE;
	return Futex(&parameters);
}

oserr_t
ConditionBroadcast(
        _In_ Condition_t* cond)
{
    FutexParameters_t parameters;
    
	if (cond == NULL) {
        return OsInvalidParameters;
	}
	
    parameters._futex0  = &cond->Value;
    parameters._val0    = atomic_load(&cond->Value);
    parameters._flags   = FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE;
	return Futex(&parameters);
}

oserr_t
ConditionWait(
        _In_ Condition_t* cond,
        _In_ Mutex_t*     mutex)
{
    FutexParameters_t parameters;
    oserr_t           oserr;
	if (cond == NULL || mutex == NULL) {
		return OsInvalidParameters;
	}

    parameters._futex0  = &cond->Value;
    parameters._futex1  = &mutex->Value;
    parameters._val0    = atomic_load(&cond->Value);
    parameters._val1    = 1; // Wakeup one on the mutex
    parameters._val2    = FUTEX_OP(FUTEX_OP_SET, 0, 0, 0);
    parameters._flags   = FUTEX_FLAG_WAIT | FUTEX_FLAG_PRIVATE | FUTEX_FLAG_OP;
    parameters._timeout = 0;

    oserr = Futex(&parameters);
    if (oserr != OsOK) {
        MutexLock(mutex);
        return oserr;
    }
    return MutexLock(mutex);
}

oserr_t
ConditionTimedWait(
        _In_ Condition_t* restrict           cond,
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec* restrict timePoint)
{
    FutexParameters_t parameters;
	oserr_t           status;
    time_t            msec;
	struct timespec   now, result;

	if (cond == NULL || mutex == NULL || timePoint == NULL) {
		return OsInvalidParameters;
	}
    
    // Calculate time to sleep
	timespec_get(&now, TIME_UTC);
    timespec_diff(&now, timePoint, &result);
    if (result.tv_sec < 0) {
        return OsTimeout;
    }

    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }

    parameters._futex0  = &cond->Value;
    parameters._futex1  = &mutex->Value;
    parameters._val0    = atomic_load(&cond->Value);
    parameters._val1    = 1; // Wakeup one on the mutex
    parameters._val2    = FUTEX_OP(FUTEX_OP_SET, 0, 0, 0); // Reset mutex to 0
    parameters._flags   = FUTEX_FLAG_WAIT | FUTEX_FLAG_PRIVATE | FUTEX_FLAG_OP;
    parameters._timeout = msec;
    
    status = Futex(&parameters);
    if (status != OsOK) {
        MutexLock(mutex);
        return status;
    }
	return MutexLock(mutex);
}
