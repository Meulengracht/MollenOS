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
 * MollenOS Resource Handle Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */

#include <assert.h>
#include <string.h>
#include <handle.h>
#include <heap.h>

// Include all the systems that we have to cleanup
#include <process/process.h>
#include <memorybuffer.h>

static Collection_t         Handles                             = COLLECTION_INIT(KeyInteger);
static _Atomic(UUId_t)      IdGenerator                         = 1;
static HandleDestructorFn   HandleDestructors[HandleTypeCount]  = {
    DestroyMemoryBuffer,
    DestroyProcess
};

/* CreateHandle
 * Allocates a new handle for a system resource with a reference of 1. */
UUId_t
CreateHandle(
    _In_ SystemHandleType_t Type,
    _In_ void*              Resource)
{
    SystemHandle_t *Handle;
    UUId_t Id;

    assert(Resource != NULL);

    // Allocate a new instance and add it to list
    Handle  = (SystemHandle_t*)kmalloc(sizeof(SystemHandle_t));
    Id      = atomic_fetch_add(&IdGenerator, 1);

    memset((void*)Handle, 0, sizeof(SystemHandle_t));
    Handle->Header.Key.Value    = (int)Id;
    Handle->Type                = Type;
    Handle->Resource            = Resource;
    atomic_store_explicit(&Handle->References, 1, memory_order_relaxed);
    CollectionAppend(&Handles, &Handle->Header);
    return Id;
}

/* AcquireHandle
 * Acquires the handle given for the calling process. This can fail if the handle
 * turns out to be invalid, otherwise the resource will be returned. */
void*
AcquireHandle(
    _In_ UUId_t             Handle)
{
    SystemHandle_t *Instance;
    DataKey_t Key;
    int PreviousReferences;

    // Lookup the handle
    Key.Value   = (int)Handle;
    Instance    = (SystemHandle_t*)CollectionGetNodeByKey(&Handles, Key, 0);
    if (Instance == NULL) {
        return NULL;
    }

    PreviousReferences = atomic_fetch_add(&Instance->References, 1);
    if (PreviousReferences == 0) {
        // Special case, to fix race-conditioning. If the reference
        // count ever reach 0 this was called on cleanup.
        return NULL;
    }
    return Instance->Resource;
}

/* LookupHandle
 * Retrieves the handle given for the calling process. This can fail if the handle
 * turns out to be invalid, otherwise the resource will be returned. */
void*
LookupHandle(
    _In_ UUId_t             Handle)
{
    SystemHandle_t* Instance;
    DataKey_t       Key;

    // Lookup the handle
    Key.Value   = (int)Handle;
    Instance    = (SystemHandle_t*)CollectionGetNodeByKey(&Handles, Key, 0);
    if (Instance == NULL) {
        return NULL;
    }
    return Instance->Resource;
}

/* DestroyHandle
 * Reduces the reference count of the given handle, and cleans up the handle on
 * reaching 0 references. */
OsStatus_t
DestroyHandle(
    _In_ UUId_t             Handle)
{
    SystemHandle_t *Instance;
    OsStatus_t Status = OsSuccess;
    DataKey_t Key;
    int References;

    // Lookup the handle
    Key.Value   = (int)Handle;
    Instance    = (SystemHandle_t*)CollectionGetNodeByKey(&Handles, Key, 0);
    if (Instance == NULL) {
        return OsError;
    }

    References = atomic_fetch_sub(&Instance->References, 1) - 1;
    if (References == 0) {
        CollectionRemoveByNode(&Handles, &Instance->Header);
        Status = HandleDestructors[Instance->Type](Instance->Resource);
        kfree(Instance);
    }
    return Status;
}
