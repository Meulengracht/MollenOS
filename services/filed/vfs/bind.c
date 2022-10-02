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

#include <vfs/vfs.h>
#include "private.h"
#include <string.h>

oserr_t VFSNodeBind(struct VFS* vfs, uuid_t fromID, uuid_t toID)
{
    struct VFSNodeHandle* fromHandle;
    struct VFSNodeHandle* toHandle;
    oserr_t               oserr;
    _CRT_UNUSED(vfs);

    oserr = VFSNodeHandleGet(fromID, &fromHandle);
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = VFSNodeHandleGet(toID, &toHandle);
    if (oserr != OsOK) {
        return oserr;
    }

    usched_rwlock_w_lock(&toHandle->Node->Lock);
    if (!__NodeIsRegular(toHandle->Node)) {
        oserr = OsInvalidParameters;
        goto exit;
    }

    toHandle->Node->Type     = VFS_NODE_TYPE_MOUNTPOINT;
    toHandle->Node->TypeData = fromHandle->Node;

    usched_mtx_lock(&fromHandle->Node->MountsLock);
    hashtable_set(
            &fromHandle->Node->Mounts,
            &(struct __VFSMount) { .Target = toHandle->Node }
    );
    usched_mtx_unlock(&fromHandle->Node->MountsLock);

exit:
    usched_rwlock_w_unlock(&toHandle->Node->Lock);
    return oserr;
}

oserr_t VFSNodeUnbind(struct VFS* vfs, uuid_t directoryHandleID)
{
    struct VFSNodeHandle* handle;
    oserr_t               oserr;
    _CRT_UNUSED(vfs);

    oserr = VFSNodeHandleGet(directoryHandleID, &handle);
    if (oserr != OsOK) {
        return oserr;
    }

    usched_rwlock_w_lock(&handle->Node->Lock);
    if (!__NodeIsBindMount(handle->Node)) {
        oserr = OsInvalidParameters;
        goto exit;
    }

    handle->Node->Type     = VFS_NODE_TYPE_REGULAR;
    handle->Node->TypeData = NULL;

exit:
    usched_rwlock_w_unlock(&handle->Node->Lock);
    return oserr;
}
