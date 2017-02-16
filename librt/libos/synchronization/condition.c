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

/* Includes
 * - System */
#include <os/thread.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

/* ConditionCreate
 * Instantiates a new condition and allocates
 * all required resources for the condition */
Condition_t *
ConditionCreate(void)
{
	/* Allocate a handle */
	Condition_t *Cond = (Condition_t*)malloc(sizeof(Condition_t));

	/* Reuse the construct 
	 * function */
	if (ConditionConstruct(Cond) != OsNoError) {
		return NULL;
	}

	/* Done! */
	return Cond;
}

/* ConditionConstruct
 * Constructs an already allocated condition
 * handle and initializes it */
OsStatus_t 
ConditionConstruct(
	_In_ Condition_t *Cond)
{
	/* Variables */
	int RetVal = 0;

	/* Sanitize all _in_ */
	if (Cond == NULL) {
		return OsError;
	}

	/* Resolve with syscall */
	RetVal = Syscall0(SYSCALL_CONDCREATE);

	/* Sanitize result */
	if (RetVal == 0) {
		return OsError;
	}

	/* Store information */
	*Cond = (Condition_t)RetVal;
	return OsNoError;
}

/* ConditionDestroy
 * Destroys a conditional variable and 
 * wakes up all remaining sleepers */
OsStatus_t 
ConditionDestroy(
	_In_ Condition_t *Cond)
{
	/* Sanitize all _in_ */
	if (Cond == NULL) {
		return OsError;
	}

	/* Redirect the call */
	return (OsStatus_t)Syscall1(SYSCALL_CONDDESTROY, SYSCALL_PARAM(*Cond));
}

/* ConditionSignal
 * Signal the condition and wakes up a thread
 * in the queue for the condition */
OsStatus_t 
ConditionSignal(
	_In_ Condition_t *Cond)
{
	/* Sanitize all _in_ */
	if (Cond == NULL) {
		return OsError;
	}

	/* Redirect call */
	return (OsStatus_t)Syscall1(SYSCALL_SYNCWAKEONE, SYSCALL_PARAM(*Cond));
}

/* ConditionBroadcast
 * Broadcast a signal to all condition waiters
 * and wakes threads up waiting for the cond */
OsStatus_t 
ConditionBroadcast(
	_In_ Condition_t *Cond)
{
	/* Sanitize all _in_ */
	if (Cond == NULL) {
		return OsError;
	}

	/* Redirect call */
	return Syscall1(SYSCALL_SYNCWAKEALL, SYSCALL_PARAM(*Cond));
}

/* ConditionWait
 * Waits for condition to be signaled, and 
 * acquires the given mutex, using multiple 
 * mutexes for same condition is undefined behaviour */
OsStatus_t 
ConditionWait(
	_In_ Condition_t *Cond,
	_In_ Mutex_t *Mutex)
{
	/* Sanitize all _in_ */
	if (Cond == NULL || Mutex == NULL) {
		return OsError;
	}

	/* Unlock mutex, enter sleep */
	MutexUnlock(Mutex);
	Syscall2(SYSCALL_SYNCSLEEP, SYSCALL_PARAM(*Cond), 
		SYSCALL_PARAM(0));

	/* Ok, we have been woken up, acquire mutex */
	if (MutexLock(Mutex) == MUTEX_SUCCESS) {
		return OsNoError;
	}
	else {
		return OsError;
	}
}

/* ConditionWaitTimed
 * This functions as the ConditionWait, 
 * but also has a timeout specified, so that 
 * we get waken up if the timeout expires (in seconds) */
OsStatus_t 
ConditionWaitTimed(
	_In_ Condition_t *Cond, 
	_In_ Mutex_t *Mutex, 
	_In_ time_t Expiration)
{
	/* Variables */
	OsStatus_t Result;
	time_t Now;

	/* Sanity!! */
	if (Cond == NULL || Mutex == NULL) {
		return OsError;
	}

	/* Initiate now and unlock mutex */
	Now = time(NULL);
	MutexUnlock(Mutex);

	/* Enter sleep */
	Result = (OsStatus_t)Syscall2(SYSCALL_SYNCSLEEP, SYSCALL_PARAM(*Cond),
		SYSCALL_PARAM(difftime(Expiration, Now) * 1000));

	/* Did we timeout ? */
	if (Result != OsNoError) {
		_set_errno(ETIMEDOUT);
		return Result;
	}
	else {
		_set_errno(EOK);
	}
	
	/* Ok, we have been woken up, acquire mutex */
	if (MutexLock(Mutex) == MUTEX_SUCCESS) {
		return OsNoError;
	}
	else {
		return OsError;
	}
}
