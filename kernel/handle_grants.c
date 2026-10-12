/**
 * Copyright, Philip Meulengracht
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
 * Resource Handle Grant Interface
 * - Implementation of the resource handle interface. This provides access
 *   control of resource handles.
 *
 */

#define __MODULE "handle_grants"
//#define __TRACE

#include <handle_grants.h>
#include <memoryspace.h>
#include <mutex.h>
#include "memory/private.h"

struct HandleGrant {
    uuid_t              ID;
    HandleGrantRights_t Rights;
};

static uint64_t handlegrant_hash(const void* element);
static int      handlegrant_cmp(const void* element1, const void* element2);

void
HandleGrantTableInitialize(
    _In_ struct HandleGrantTable* table)
{
    int status;

    MutexConstruct(&table->SyncObject, MUTEX_FLAG_PLAIN);

    status = hashtable_construct(
        &table->Entries,
        HASHTABLE_MINIMUM_CAPACITY,
        sizeof(struct HandleGrant),
        handlegrant_hash,
        handlegrant_cmp
    );
}

static void 
__CloseGrant(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       userContext)
{
    const struct HandleGrant* entry = element;
    (void)index;
    (void)userContext;
    (void)DestroyHandle(entry->ID);
}

void
HandleGrantTableClose(
    _In_ struct HandleGrantTable* table)
{
    MutexLock(&table->SyncObject);
    hashtable_enumerate(&table->Entries, __CloseGrant, NULL);
    hashtable_destroy(&table->Entries);
    MutexUnlock(&table->SyncObject);
}

void
HandleGrantTableDestroy(
    _In_ struct HandleGrantTable* table)
{
    HandleGrantTableClose(table);
    hashtable_destroy(&table->Entries);
    MutexDestruct(&table->SyncObject);
}

oserr_t
HandleGrantAdd(
    _In_ uuid_t              id,
    _In_ HandleGrantRights_t rights)
{
    MemorySpace_t*     current = GetCurrentMemorySpace();
    oserr_t            oserr;
    struct HandleGrant entry = {
        .ID = id,
        .Rights = rights
    };

    // Must be a part of the thread cluster
    if (current == NULL || current->Context == NULL) {
        return OS_ENOTSUPPORTED;
    }

    MutexLock(&current->Context->HandleGrants.SyncObject);
    // Something is really wrong if the entry already exists
    // And we have no protocol for updating rights atp.
    if (hashtable_get(&current->Context->HandleGrants.Entries, &entry) != NULL) {
        MutexUnlock(&current->Context->HandleGrants.SyncObject);
        return OS_EEXISTS;
    }

    oserr = AcquireHandle(id, NULL);
    if (oserr != OS_EOK) {
        hashtable_set(&current->Context->HandleGrants.Entries, &entry);
    }
    MutexUnlock(&current->Context->HandleGrants.SyncObject);
    return oserr;
}

oserr_t
HandleGrantAcquire(
    _In_ uuid_t              id,
    _In_ HandleType_t        handleType,
    _In_ HandleGrantRights_t rights,
    _In_ void**              resourceOut)
{
    MemorySpace_t*      current = GetCurrentMemorySpace();
    oserr_t             oserr;
    struct HandleGrant* entry;

    // If context is not available, then we are inside kernel
    // operations. In this case we redirect to the un-controlled
    // system.
    if (current == NULL || current->Context == NULL) {
        return AcquireHandleOfType(id, handleType, resourceOut);
    }

    MutexLock(&current->Context->HandleGrants.SyncObject);
    entry = hashtable_get(
        &current->Context->HandleGrants.Entries,
        &((struct HandleGrant) { .ID = id })
    );
    if (entry == NULL) {
        MutexUnlock(&current->Context->HandleGrants.SyncObject);
        return OS_ENOENT;
    }

    // Check if the requested rights are allowed
    if ((entry->Rights & rights) != rights) {
        MutexUnlock(&current->Context->HandleGrants.SyncObject);
        return OS_EPERMISSIONS;
    }

    oserr = AcquireHandleOfType(id, handleType, resourceOut);
    MutexUnlock(&current->Context->HandleGrants.SyncObject);
    return oserr;
}

oserr_t
HandleGrantRelease(
    _In_ uuid_t id)
{
    MemorySpace_t*     current = GetCurrentMemorySpace();
    oserr_t            oserr;
    struct HandleGrant entry = {
        .ID = id
    };

    // Must be a part of the thread cluster
    if (current == NULL || current->Context == NULL) {
        return OS_ENOTSUPPORTED;
    }

    MutexLock(&current->Context->HandleGrants.SyncObject);
    if (!hashtable_get(&current->Context->HandleGrants.Entries, &entry)) {
        MutexUnlock(&current->Context->HandleGrants.SyncObject);
        return OS_ENOENT;
    }

    // Release the handle before removing the grant entry
    oserr = DestroyHandle(id);
    hashtable_remove(&current->Context->HandleGrants.Entries, &entry);
    MutexUnlock(&current->Context->HandleGrants.SyncObject);
    return oserr;
}

static uint64_t handlegrant_hash(const void* element)
{
    const struct HandleGrant* entry = element;
    return entry->ID; // already unique identifier
}

static int handlegrant_cmp(const void* element1, const void* element2)
{
    const struct HandleGrant* lh = element1;
    const struct HandleGrant* rh = element2;

    // return 0 on true, 1 on false
    return lh->ID == rh->ID ? 0 : 1;
}
