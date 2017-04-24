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
 * MollenOS MCore - Semaphore (Binary) Support Definitions & Structures
 * - This header describes the binary semaphore-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _BINARYSEMAPHORE_INTERFACE_H_
#define _BINARYSEMAPHORE_INTERFACE_H_

/* Includes
 * - System */
#include <os/osdefs.h>
#include <os/condition.h>

/* Binary Semaphore
 * Provides a synchronization method between threads and jobs */
typedef struct _BinarySemaphore {
	Mutex_t					Mutex;
	Condition_t				Condition;
	int						Value;
} BinarySemaphore_t;

/* BinarySemaphoreConstruct
 * Initializes the semaphore value to either 0 or 1. The pointer
 * must be pre-allocated before calling. */
MOSAPI
OsStatus_t
MOSABI
BinarySemaphoreConstruct(
	_In_ BinarySemaphore_t *BinarySemaphore,
	_In_ int Value);

/* BinarySemaphoreReset
 * Reinitializes the semaphore with a value of 0 */
MOSAPI
OsStatus_t
MOSABI
BinarySemaphoreReset(
	_In_ BinarySemaphore_t *BinarySemaphore);

/* BinarySemaphorePost
 * Post event to a single thread waiting for an event */
MOSAPI
void
MOSABI
BinarySemaphorePost(
	_In_ BinarySemaphore_t *BinarySemaphore);

/* BinarySemaphorePostAll
 * Post event to all threads waiting for an event */
MOSAPI
void
MOSABI
BinarySemaphorePostAll(
	_In_ BinarySemaphore_t *BinarySemaphore);

/* BinarySemaphoreWait
 * Wait on semaphore until semaphore has value 0 */
MOSAPI
void
MOSABI
BinarySemaphoreWait(
	_In_ BinarySemaphore_t* BinarySemaphore);

#endif //!_BINARYSEMAPHORE_INTERFACE_H_
