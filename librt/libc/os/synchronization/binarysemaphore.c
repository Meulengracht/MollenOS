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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Condition Support Definitions & Structures
 * - This header describes the base condition-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/binarysemaphore.h>
#include <ddk/utils.h>

/* BinarySemaphoreConstruct
 * Initializes the semaphore value to either 0 or 1 */
oscode_t
BinarySemaphoreConstruct(
	_In_ BinarySemaphore_t *BinarySemaphore, 
	_In_ int Value)
{
	// Trace
	TRACE("BinarySemaphoreConstruct(%i)", Value);

	// Sanitize Parameters
	if (Value < 0 || Value > 1) {
		ERROR("Binary semaphore can take only values 1 or 0");
		return OsError;
	}

	// Initialize resources
    mtx_init(&BinarySemaphore->Mutex, mtx_plain);
    cnd_init(&BinarySemaphore->Condition);
	BinarySemaphore->Value = Value;
	return OsOK;
}

/* BinarySemaphoreReset
 * Reinitializes the semaphore with a value of 0 */
oscode_t
BinarySemaphoreReset(
	_In_ BinarySemaphore_t *BinarySemaphore)
{
	return BinarySemaphoreConstruct(BinarySemaphore, 0);
}

/* BinarySemaphorePost
 * Post event to a single thread waiting for an event */
void
BinarySemaphorePost(
	_In_ BinarySemaphore_t *BinarySemaphore)
{
	// Lock mutex
	mtx_lock(&BinarySemaphore->Mutex);

	// Set value to 1, and signal a thread
	BinarySemaphore->Value = 1;
	cnd_signal(&BinarySemaphore->Condition);
	mtx_unlock(&BinarySemaphore->Mutex);
}

/* BinarySemaphorePostAll
 * Post event to all threads waiting for an event */
void
BinarySemaphorePostAll(
	_In_ BinarySemaphore_t *BinarySemaphore)
{
	// Lock mutex
	mtx_lock(&BinarySemaphore->Mutex);

	// Set value to 1, and signal a thread
	BinarySemaphore->Value = 1;
	cnd_broadcast(&BinarySemaphore->Condition);
	mtx_unlock(&BinarySemaphore->Mutex);
}

/* BinarySemaphoreWait
 * Wait on semaphore until semaphore has value 0 */
void
BinarySemaphoreWait(
	_In_ BinarySemaphore_t* BinarySemaphore)
{
	// Lock mutex
	mtx_lock(&BinarySemaphore->Mutex);

	// Wait for value to become set before waiting
	while (BinarySemaphore->Value != 1) {
		cnd_wait(&BinarySemaphore->Condition, &BinarySemaphore->Mutex);
	}
	BinarySemaphore->Value = 0;
	mtx_unlock(&BinarySemaphore->Mutex);
}
