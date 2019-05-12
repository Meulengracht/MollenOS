/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Synchronization (Semaphore)
 * - Counting semaphores implementation
 */

#ifndef __SEMAPHORE_H__
#define __SEMAPHORE_H__

#include <os/osdefs.h>
#include <scheduler.h>
#include <mutex.h>

typedef struct {
    SchedulerLockedQueue_t Queue;
	_Atomic(int)           Value;
    int                    MaxValue;
} Semaphore_t;

#define SEMAPHORE_INIT(Value, MaxValue) { SCHEDULER_LOCKED_QUEUE_INIT, ATOMIC_VAR_INIT(Value), MaxValue }

/* SemaphoreConstruct
 * Initializes a semaphore to default values. */
KERNELAPI void KERNELABI
SemaphoreConstruct(
    _In_ Semaphore_t* Semaphore,
    _In_ int          InitialValue,
    _In_ int          MaximumValue);

/* SemaphoreWaitSimple
 * Waits for the semaphore signal. */
KERNELAPI OsStatus_t KERNELABI
SemaphoreWaitSimple(
    _In_ Semaphore_t* Semaphore,
    _In_ size_t       Timeout);

/* SemaphoreWait
 * Waits for the semaphore signal, with the synchronization of a mutex. */
KERNELAPI OsStatus_t KERNELABI
SemaphoreWait(
    _In_ Semaphore_t* Semaphore,
    _In_ Mutex_t*     Mutex,
    _In_ size_t       Timeout);

/* SemaphoreSignal
 * Signals the semaphore with the given value, default is 1 */
KERNELAPI void KERNELABI
SemaphoreSignal(
    _In_ Semaphore_t* Semaphore,
    _In_ int          Value);

#endif // !__SEMAPHORE_H__
