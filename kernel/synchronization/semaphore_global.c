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

#include <semaphore_global.h>
#include <assert.h>
#include <heap.h>

static Collection_t Semaphores = COLLECTION_INIT(KeyId);

OsStatus_t
CreateGlobalSemaphore(
    _In_  MString_t*            Identifier, 
    _In_  int                   InitialValue,
    _In_  int                   MaximumValue,
    _Out_ GlobalSemaphore_t**   Semaphore)
{
	GlobalSemaphore_t*  Instance = NULL;
	DataKey_t           Key      = { 0 };

	assert(InitialValue >= 0);
    assert(MaximumValue >= InitialValue);

	// First of all, make sure there is no conflicting semaphores in system
	if (Identifier != NULL) {
		Key.Value.Id = MStringHash(Identifier);
		void *Exists = CollectionGetNodeByKey(&Semaphores, Key, 0);
		if (Exists != NULL) {
			*Semaphore = (GlobalSemaphore_t*)Exists;
            return OsError;
		}
	}

    // Initialize the new instance
	Instance = (GlobalSemaphore_t*)kmalloc(sizeof(GlobalSemaphore_t));
    memset(Instance, 0, sizeof(GlobalSemaphore_t));
	SlimSemaphoreConstruct(&Instance->Semaphore, InitialValue, MaximumValue);
    Instance->Header.Key = Key;
	
    if (Identifier != NULL)  {
        Instance->Hash = MStringHash(Identifier);
		CollectionAppend(&Semaphores, &Instance->Header);
	}
    else {
        Instance->Hash = 0;
    }
    
	*Semaphore = Instance;
    return OsSuccess;
}

void
DestroyGlobalSemaphore(
    _In_ GlobalSemaphore_t*     Semaphore)
{
	DataKey_t Key;
    
	assert(Semaphore != NULL);
	if (Semaphore->Hash != 0) {
		Key.Value.Id = Semaphore->Hash;
		CollectionRemoveByNode(&Semaphores, &Semaphore->Header);
	}
	SlimSemaphoreDestroy(&Semaphore->Semaphore);
    kfree(Semaphore);
}

int
GlobalSemaphoreWait(
    _In_ GlobalSemaphore_t*     Semaphore,
    _In_ size_t                 Timeout)
{
	return SlimSemaphoreWait(&Semaphore->Semaphore, Timeout);
}

OsStatus_t
GlobalSemaphoreSignal(
    _In_ GlobalSemaphore_t*     Semaphore,
    _In_ int                    Value)
{
	return SlimSemaphoreSignal(&Semaphore->Semaphore, Value);
}
