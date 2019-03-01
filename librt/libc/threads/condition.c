/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Condition Support Definitions & Structures
 * - This header describes the base condition-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <threads.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

int
cnd_init(
    _In_ cnd_t* cond)
{
    // Sanitize input
    if (cond == NULL) {
        return thrd_error;
    }
    if (Syscall_ConditionCreate(cond) != OsSuccess) {
        return thrd_error;
    }
    return thrd_success;
}

void
cnd_destroy(
    _In_ cnd_t* cond)
{
    // Sanitize input
	if (cond == NULL) {
		return;
	}
    Syscall_ConditionDestroy(*cond);
}

int
cnd_signal(
    _In_ cnd_t *cond)
{
	// Sanitize input
	if (cond == NULL) {
		return thrd_error;
	}
	if (Syscall_SignalHandle(*cond) != OsSuccess) {
        return thrd_error;
    }
    return thrd_success;
}

int 
cnd_broadcast(
    _In_ cnd_t *cond)
{
	// Sanitize input
	if (cond == NULL) {
		return thrd_error;
	}
    if (Syscall_BroadcastHandle(*cond) != OsSuccess) {
        return thrd_error;
    }
    return thrd_success;
}

int
cnd_wait(
    _In_ cnd_t* cond,
    _In_ mtx_t* mutex)
{
	if (cond == NULL || mutex == NULL) {
		return thrd_error;
	}

	// Unlock mutex and sleep
	if (mtx_unlock(mutex) != thrd_success) {
        return thrd_error;
    }
	Syscall_WaitForObject(*cond, 0);
    return mtx_lock(mutex);
}

int
cnd_timedwait(
    _In_ cnd_t* restrict cond,
    _In_ mtx_t* restrict mutex,
    _In_ const struct timespec* restrict time_point)
{
	// Variables
	OsStatus_t osresult = OsError;
	struct timespec     now, result;
    time_t msec         = 0;

	// Sanitize input
	if (cond == NULL || mutex == NULL) {
		return thrd_error;
	}

	// Prepare to sleep-wait
    if (mtx_unlock(mutex) != thrd_success) {
        return thrd_error;
    }
	timespec_get(&now, TIME_UTC);
    timespec_diff(time_point, &now, &result);
    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }
	osresult = Syscall_WaitForObject(*cond, msec);
	if (osresult != OsSuccess) {
		return thrd_timedout;
	}
	
	// Last step is to acquire mutex again
	return mtx_lock(mutex);
}
