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

}

/* Constructs an already allocated condition
* handle and initializes it */
_MOS_API int ConditionConstruct(Condition_t *Cond);

/* Destroys a conditional variable and 
 * wakes up all remaining sleepers */
_MOS_API void ConditionDestroy(Condition_t *Cond);

/* Signal the condition and wakes up a thread
* in the queue for the condition */
_MOS_API int ConditionSignal(Condition_t *Cond);

/* Broadcast a signal to all condition waiters
* and wakes threads up waiting for the cond */
_MOS_API int ConditionBroadcast(Condition_t *Cond);

/* Waits for condition to be signaled, and
* acquires the given mutex, using multiple
* mutexes for same condition is undefined behaviour */
_MOS_API int ConditionWait(Condition_t *Cond, Mutex_t *Mutex);

/* This functions as the ConditionWait,
* but also has a timeout specified, so that
* we get waken up if the timeout expires (in seconds) */
_MOS_API int ConditionWaitTimed(Condition_t *Cond, Mutex_t *Mutex, size_t Timeout);

#endif