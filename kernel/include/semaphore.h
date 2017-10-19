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
 * - Semaphores implementation, used safe passages known as
 *   critical sections in MCore
 */

#ifndef _MCORE_SEMAPHORE_H_
#define _MCORE_SEMAPHORE_H_

/* Includes 
 * - Systems */
#include <os/osdefs.h>
#include <criticalsection.h>

/* Includes
 * - Library */
#include <ds/mstring.h>

/* The semaphore structure, contains
 * an internal safe-passge lock, a creator id
 * and a Hash (for global support), and the current value */
typedef struct _Semaphore {
	CriticalSection_t Lock;
	UUId_t Creator;
	size_t Hash;
	int Value;
} Semaphore_t;

/* SemaphoreCreate
 * Initializes and allocates a new semaphore
 * Semaphores use safe passages to avoid race-conditions */
__EXTERN Semaphore_t *SemaphoreCreate(int InitialValue);

/* SemaphoreCreateGlobal
 * Creates a global semaphore, identified by it's name
 * and makes sure only one can exist at the time. Returns
 * NULL if one already exists. */
__EXTERN Semaphore_t *SemaphoreCreateGlobal(MString_t *Identifier, int InitialValue);

/* SemaphoreConstruct
 * Constructs an already allocated semaphore and resets
 * it's value to the given initial value */
__EXTERN void SemaphoreConstruct(Semaphore_t *Semaphore, int InitialValue);

/* SemaphoreDestroy
 * Destroys and frees a semaphore, releasing any
 * resources associated with it */
__EXTERN void SemaphoreDestroy(Semaphore_t *Semaphore);

/* SemaphoreP (Wait) 
 * Waits for the semaphore signal with the optional time-out */
__EXTERN OsStatus_t SemaphoreP(Semaphore_t *Semaphore, size_t Timeout);

/* SemaphoreV (Signal) 
 * Signals the semaphore with the given value, default is 1 */
__EXTERN void SemaphoreV(Semaphore_t *Semaphore, int Value);

#endif // !_MCORE_SEMAPHORE_H_
