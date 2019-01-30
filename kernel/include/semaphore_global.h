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

#ifndef _MCORE_SEMAPHORE_H_
#define _MCORE_SEMAPHORE_H_

#include <os/osdefs.h>
#include <ds/collection.h>
#include <ds/mstring.h>
#include <semaphore_slim.h>

/* GlobalSemaphore
 * Global semaphores need an identifier (string) contrary to normal semaphores.
 * Only one semaphore with the identifier can exist */
typedef struct _GlobalSemaphore {
    CollectionItem_t Header;
    SlimSemaphore_t  Semaphore;
    UUId_t           Creator;
    size_t           Hash;
} GlobalSemaphore_t;

/* CreateGlobalSemaphore
 * Allocates a completely new instance of the global semaphore. If a semaphore with
 * the given name exists, the existing semaphore is returned and OsError. */
KERNELAPI OsStatus_t KERNELABI
CreateGlobalSemaphore(
    _In_  MString_t*            Identifier, 
    _In_  int                   InitialValue,
    _In_  int                   MaximumValue,
    _Out_ GlobalSemaphore_t**   Semaphore);

/* DestroyGlobalSemaphore
 * Wakes up all threads that are waiting for the semaphore and destroys the semaphore. */
KERNELAPI void KERNELABI
DestroyGlobalSemaphore(
    _In_ GlobalSemaphore_t*     Semaphore);

/* GlobalSemaphoreWait
 * Waits for the semaphore signal with the optional time-out.
 * Returns SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT */
KERNELAPI int KERNELABI
GlobalSemaphoreWait(
    _In_ GlobalSemaphore_t*     Semaphore,
    _In_ size_t                 Timeout);

/* GlobalSemaphoreSignal
 * Signals the semaphore with the given value, default is 1 */
KERNELAPI OsStatus_t KERNELABI
GlobalSemaphoreSignal(
    _In_ GlobalSemaphore_t*     Semaphore,
    _In_ int                    Value);

#endif // !_MCORE_SEMAPHORE_H_
