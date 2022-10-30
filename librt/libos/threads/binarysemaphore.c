/* MollenOS
 *
 * Copyright 2022, Philip Meulengracht
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
 */

#include <os/binarysemaphore.h>

oserr_t
BinarySemaphoreInitialize(
        _In_ BinarySemaphore_t* binarySemaphore,
        _In_ int                value)
{
	if (value < 0 || value > 1) {
		return OS_EUNKNOWN;
	}

    MutexInitialize(&binarySemaphore->Mutex, MUTEX_PLAIN);
    ConditionInitialize(&binarySemaphore->Condition);
    binarySemaphore->Value = value;
	return OS_EOK;
}

void
BinarySemaphoreReset(
        _In_ BinarySemaphore_t* binarySemaphore)
{
    (void)BinarySemaphoreInitialize(binarySemaphore, 0);
}

void
BinarySemaphorePost(
        _In_ BinarySemaphore_t* binarySemaphore)
{
	MutexLock(&binarySemaphore->Mutex);

	// Set value to 1, and signal a thread
    binarySemaphore->Value = 1;
	ConditionSignal(&binarySemaphore->Condition);
	MutexUnlock(&binarySemaphore->Mutex);
}

void
BinarySemaphorePostAll(
        _In_ BinarySemaphore_t* binarySemaphore)
{
	MutexLock(&binarySemaphore->Mutex);

	// Set value to 1, and signal a thread
    binarySemaphore->Value = 1;
	ConditionBroadcast(&binarySemaphore->Condition);
	MutexUnlock(&binarySemaphore->Mutex);
}

void
BinarySemaphoreWait(
        _In_ BinarySemaphore_t* binarySemaphore)
{
	MutexLock(&binarySemaphore->Mutex);

	// Wait for value to become set before waiting
	while (binarySemaphore->Value != 1) {
		ConditionWait(&binarySemaphore->Condition, &binarySemaphore->Mutex, NULL);
	}
    binarySemaphore->Value = 0;
	MutexUnlock(&binarySemaphore->Mutex);
}
