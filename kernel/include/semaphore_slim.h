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
 * MollenOS Synchronization
 * - Counting semaphores implementation, using safe passages known as
 *   atomic sections in the operating system to synchronize in a kernel env
 */

#ifndef __SLIM_SEMAPHORE__
#define __SLIM_SEMAPHORE__

#include <os/osdefs.h>

typedef struct {
	atomic_int Value;
    int        MaxValue;
} SlimSemaphore_t;

/* SlimSemaphoreConstruct
 * Constructs an already allocated semaphore and resets
 * it's value to the given initial value */
KERNELAPI void KERNELABI
SlimSemaphoreConstruct(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ int                InitialValue,
    _In_ int                MaximumValue);

/* SlimSemaphoreDestroy
 * Cleans up the semaphore, waking up all sleeper threads
 * to not have dead threads. */
KERNELAPI void KERNELABI
SlimSemaphoreDestroy(
    _In_ SlimSemaphore_t*   Semaphore);

/* SlimSemaphoreWait
 * Waits for the semaphore signal with the optional time-out.
 * Returns SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT */
KERNELAPI int KERNELABI
SlimSemaphoreWait(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ size_t             Timeout);

/* SlimSemaphoreSignal
 * Signals the semaphore with the given value, default is 1 */
KERNELAPI OsStatus_t KERNELABI
SlimSemaphoreSignal(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ int                Value);

#endif // !__SLIM_SEMAPHORE__
