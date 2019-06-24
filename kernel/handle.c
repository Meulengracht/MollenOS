/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Resource Handle Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */
#define __MODULE "HNDL"
//#define __TRACE

#include <arch/thread.h>
#include <scheduler.h>
#include <assert.h>
#include <string.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

// Include all the systems that we have to cleanup
#include <memoryspace.h>
#include <pipe.h>

static Collection_t       SystemHandles                       = COLLECTION_INIT(KeyId);
static _Atomic(UUId_t)    IdGenerator                         = 1;
static HandleDestructorFn HandleDestructors[HandleTypeCount]  = {
    NULL,                      // Generic - Ignore
    DestroyMemorySpace,
    MemoryDestroySharedRegion,
    DestroySystemPipe
};

UUId_t
CreateHandle(
    _In_ SystemHandleType_t         Type,
    _In_ void*                      Resource)
{
    SystemHandle_t* Handle;
    UUId_t          Id;

    assert(Resource != NULL);

    // Allocate a new instance and add it to list
    Handle  = (SystemHandle_t*)kmalloc(sizeof(SystemHandle_t));
    Id      = atomic_fetch_add(&IdGenerator, 1);

    memset((void*)Handle, 0, sizeof(SystemHandle_t));
    Handle->Header.Key.Value.Id = Id;
    Handle->Type                = Type;
    Handle->Resource            = Resource;
    atomic_store_explicit(&Handle->References, 1, memory_order_relaxed);
    CollectionAppend(&SystemHandles, &Handle->Header);
    return Id;
}

void*
AcquireHandle(
    _In_ UUId_t             Handle)
{
    SystemHandle_t* Instance;
    DataKey_t       Key;
    int             PreviousReferences;

    // Lookup the handle
    Key.Value.Id    = Handle;
    Instance        = (SystemHandle_t*)CollectionGetNodeByKey(&SystemHandles, Key, 0);
    if (Instance == NULL) {
        return NULL;
    }

    PreviousReferences = atomic_fetch_add(&Instance->References, 1);
    if (PreviousReferences == 0) {
        // Special case, to prevent race-conditioning. If the reference
        // count ever reach 0 this was called on cleanup.
        return NULL;
    }
    return Instance->Resource;
}

void*
LookupHandle(
    _In_ UUId_t             Handle)
{
    SystemHandle_t* Instance;
    DataKey_t       Key;

    // Lookup the handle
    Key.Value.Id    = Handle;
    Instance        = (SystemHandle_t*)CollectionGetNodeByKey(&SystemHandles, Key, 0);
    if (Instance == NULL) {
        return NULL;
    }
    return Instance->Resource;
}

void*
LookupHandleOfType(
    _In_ UUId_t             Handle,
    _In_ SystemHandleType_t Type)
{
    SystemHandle_t* Instance;
    DataKey_t       Key;

    // Lookup the handle
    Key.Value.Id    = Handle;
    Instance        = (SystemHandle_t*)CollectionGetNodeByKey(&SystemHandles, Key, 0);
    if (Instance == NULL || Instance->Type != Type) {
        return NULL;
    }
    return Instance->Resource;
}

OsStatus_t
DestroyHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance;
    OsStatus_t      Status = OsSuccess;
    DataKey_t       Key;
    int             References;

    // Lookup the handle
    Key.Value.Id    = Handle;
    Instance        = (SystemHandle_t*)CollectionGetNodeByKey(&SystemHandles, Key, 0);
    if (Instance == NULL) {
        return OsError;
    }

    References = atomic_fetch_sub(&Instance->References, 1) - 1;
    if (References == 0) {
        CollectionRemoveByNode(&SystemHandles, &Instance->Header);
        Status = (HandleDestructors[Instance->Type] != NULL) ? HandleDestructors[Instance->Type](Instance->Resource) : OsSuccess;
        kfree(Instance);
    }
    return Status;
}
