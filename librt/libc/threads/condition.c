/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 *
 * Condition Support Definitions & Structures
 * - This header describes the base condition-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <errno.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/futex.h>
#include <threads.h>
#include <time.h>

int
cnd_init(
    _In_ cnd_t* cond)
{
    if (!cond) {
        return thrd_error;
    }

    atomic_store(&cond->syncobject, 0);
    return thrd_success;
}

void
cnd_destroy(
    _In_ cnd_t* cond)
{
	if (!cond) {
		return;
	}
	cnd_broadcast(cond);
}

int
cnd_signal(
    _In_ cnd_t* cond)
{
    FutexParameters_t parameters;
    OsStatus_t        status;
    
	if (cond == NULL) {
		return thrd_error;
	}
	
    parameters._futex0  = &cond->syncobject;
    parameters._val0    = 1;
    parameters._flags   = FUTEX_WAKE_PRIVATE;
	status = Syscall_FutexWake(&parameters);
	if (status != OsSuccess && status != OsDoesNotExist) {
	    return thrd_error;
	}
    return thrd_success;
}

int 
cnd_broadcast(
    _In_ cnd_t *cond)
{
    FutexParameters_t parameters;
    
	if (!cond) {
		return thrd_error;
	}
	
    parameters._futex0  = &cond->syncobject;
    parameters._val0    = atomic_load(&cond->syncobject);
    parameters._flags   = FUTEX_WAKE_PRIVATE;
	(void)Syscall_FutexWake(&parameters);
    return thrd_success;
}

int
cnd_wait(
    _In_ cnd_t* cond,
    _In_ mtx_t* mutex)
{
    FutexParameters_t parameters;
    OsStatus_t        status;
	if (!cond || !mutex) {
		return thrd_error;
	}

    parameters._futex0  = &cond->syncobject;
    parameters._futex1  = &mutex->value;
    parameters._val0    = atomic_load(&cond->syncobject);
    parameters._val1    = 1; // Wakeup one on the mutex
    parameters._val2    = FUTEX_OP(FUTEX_OP_SET, 0, 0, 0);
    parameters._flags   = FUTEX_WAIT_PRIVATE | FUTEX_WAIT_OP;
    parameters._timeout = 0;
    
    status = Syscall_FutexWait(&parameters);
    mtx_lock(mutex);
    if (status != OsSuccess) {
        return thrd_error;
    }
    return thrd_success;
}

int
cnd_timedwait(
    _In_ cnd_t* restrict                 cond,
    _In_ mtx_t* restrict                 mutex,
    _In_ const struct timespec* restrict time_point)
{
    FutexParameters_t parameters;
	OsStatus_t        status;
    time_t            msec;
	struct timespec   now, result;

	if (!cond || !mutex) {
		return thrd_error;
	}
    
    // Calculate time to sleep
	timespec_get(&now, TIME_UTC);
    timespec_diff(&now, time_point, &result);
    if (result.tv_sec < 0) {
        return thrd_timedout;
    }

    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }

    parameters._futex0  = &cond->syncobject;
    parameters._futex1  = &mutex->value;
    parameters._val0    = atomic_load(&cond->syncobject);
    parameters._val1    = 1; // Wakeup one on the mutex
    parameters._val2    = FUTEX_OP(FUTEX_OP_SET, 0, 0, 0); // Reset mutex to 0
    parameters._flags   = FUTEX_WAIT_PRIVATE | FUTEX_WAIT_OP;
    parameters._timeout = msec;
    
    status = Syscall_FutexWait(&parameters);
    mtx_lock(mutex);
	if (status  == OsTimeout) {
		return thrd_timedout;
	}
	else if (status != OsSuccess) {
	    return thrd_error;
	}
	return thrd_success;
}
