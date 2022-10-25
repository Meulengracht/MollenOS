/**
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
 *
 */

#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <vfs/vfs.h>
#include "private.h"

static uint64_t __HandlesHash(const void* element);
static int      __HandlesCmp(const void* lh, const void* rh);

static hashtable_t          g_handles;
static struct usched_rwlock g_handlesLock;

void
VFSNodeHandleInitialize(void)
{
    usched_rwlock_init(&g_handlesLock);
    hashtable_construct(&g_handles, 0,
                        sizeof(struct VFSNodeHandle),
                        __HandlesHash, __HandlesCmp);
}

oserr_t
VFSNodeHandleAdd(
        _In_ uuid_t          handleId,
        _In_ struct VFSNode* node,
        _In_ void*           data,
        _In_ uint32_t        accessKind)
{
    void* existing;

    usched_rwlock_w_lock(&g_handlesLock);
    existing = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handleId });
    if (existing != NULL) {
        usched_rwlock_w_unlock(&g_handlesLock);
        return OS_EEXISTS;
    }

    hashtable_set(&g_handles, &(struct VFSNodeHandle) {
        .Id = handleId,
        .Position = 0,
        .Data = data,
        .Node = node,
        .AccessKind = accessKind,
        .Mode = MODE_NONE
    });
    usched_rwlock_w_unlock(&g_handlesLock);
    return OS_EOK;
}

oserr_t
VFSNodeHandleRemove(
        _In_ uuid_t handleId)
{
    struct VFSNodeHandle* handle;

    usched_rwlock_w_lock(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handleId });
    if (handle == NULL) {
        usched_rwlock_w_unlock(&g_handlesLock);
        return OS_ENOENT;
    }

    hashtable_remove(&g_handles, handle);
    usched_rwlock_w_unlock(&g_handlesLock);
    return OS_EOK;
}

oserr_t
VFSNodeHandleGet(
        _In_  uuid_t                 handleId,
        _Out_ struct VFSNodeHandle** handleOut)
{
    struct VFSNodeHandle* handle;

    usched_rwlock_r_lock(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handleId });
    if (handle == NULL) {
        usched_rwlock_r_unlock(&g_handlesLock);
        return OS_ENOENT;
    }
    *handleOut = handle;
    return OS_EOK;
}

void
VFSNodeHandlePut(
        _In_ struct VFSNodeHandle* handle)
{
    if (handle == NULL) {
        return;
    }
    usched_rwlock_r_unlock(&g_handlesLock);
}

static uint64_t __HandlesHash(const void* element)
{
    const struct VFSNodeHandle* handle = element;
    return handle->Id;
}

static int __HandlesCmp(const void* lh, const void* rh)
{
    const struct VFSNodeHandle* handle1 = lh;
    const struct VFSNodeHandle* handle2 = rh;
    return handle1->Id == handle2->Id ? 0 : 1;
}
