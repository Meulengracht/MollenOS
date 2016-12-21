/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS - Condition Synchronization Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* C Library */
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#ifdef LIBC_KERNEL
void __ConditionLibCEmpty(void)
{
}
#else

/* Instantiates a new condition and allocates
 * all required resources for the condition */
Condition_t *ConditionCreate(void)
{
	/* Allocate a handle */
	Condition_t *Cond = (Condition_t*)malloc(sizeof(Condition_t));

	/* Reuse the construct 
	 * function */
	ConditionConstruct(Cond);

	/* Done! */
	return Cond;
}

/* Constructs an already allocated condition
 * handle and initializes it */
int ConditionConstruct(Condition_t *Cond)
{
	/* Sanity!! */
	if (Cond == NULL)
		return -1;

	/* Contact OS */
	int RetVal = Syscall0(SYSCALL_CONDCREATE);

	/* Sanity */
	if (RetVal == 0) {
		/* Fucked up */
		return -1;
	}

	/* Store information */
	*Cond = (Condition_t)RetVal;

	/* Done! */
	return 0;
}

/* Destroys a conditional variable and 
 * wakes up all remaining sleepers */
void ConditionDestroy(Condition_t *Cond)
{
	/* Sanity!! */
	if (Cond == NULL)
		return;

	/* Contact OS */
	Syscall1(SYSCALL_CONDDESTROY, SYSCALL_PARAM(*Cond));
}

/* Signal the condition and wakes up a thread
 * in the queue for the condition */
int ConditionSignal(Condition_t *Cond)
{
	/* Sanity!! */
	if (Cond == NULL)
		return -1;

	/* Contact OS */
	Syscall1(SYSCALL_SYNCWAKEONE, SYSCALL_PARAM(*Cond));

	/* Done! */
	return 0;
}

/* Broadcast a signal to all condition waiters
 * and wakes threads up waiting for the cond */
int ConditionBroadcast(Condition_t *Cond)
{
	/* Sanity!! */
	if (Cond == NULL)
		return -1;

	/* Contact OS */
	Syscall1(SYSCALL_SYNCWAKEALL, SYSCALL_PARAM(*Cond));

	/* Done! */
	return 0;
}

/* Waits for condition to be signaled, and
 * acquires the given mutex, using multiple
 * mutexes for same condition is undefined behaviour */
int ConditionWait(Condition_t *Cond, Mutex_t *Mutex)
{
	/* Sanity!! */
	if (Cond == NULL
		|| Mutex == NULL)
		return -1;

	/* Unlock mutex, enter sleep */
	MutexUnlock(Mutex);

	/* Enter sleep */
	Syscall2(SYSCALL_SYNCSLEEP, 
		SYSCALL_PARAM(*Cond), SYSCALL_PARAM(0));

	/* Ok, we have been woken up, acquire mutex */
	if (MutexLock(Mutex) == MUTEX_SUCCESS) {
		return 0;
	}
	else
		return -1;
}

/* This functions as the ConditionWait,
 * but also has a timeout specified, so that
 * we get waken up if the timeout expires (in seconds) */
int ConditionWaitTimed(Condition_t *Cond, Mutex_t *Mutex, time_t Expiration)
{
	/* Variables */
	int RetVal = 0;
	time_t Now;

	/* Sanity!! */
	if (Cond == NULL
		|| Mutex == NULL)
		return -1;

	/* Calculate timeout */
	Now = time(NULL);

	/* Unlock mutex, enter sleep */
	MutexUnlock(Mutex);

	/* Enter sleep */
	RetVal = Syscall2(SYSCALL_SYNCSLEEP, SYSCALL_PARAM(*Cond),
		SYSCALL_PARAM(difftime(Expiration, Now) * 1000));

	/* Did we timeout ? */
	if (RetVal != 0) {
		_set_errno(ETIMEDOUT);
		return -1;
	}
	else
		_set_errno(EOK);

	/* Ok, we have been woken up, acquire mutex */
	if (MutexLock(Mutex) == MUTEX_SUCCESS) {
		return 0;
	}
	else
		return -1;
}

#endif