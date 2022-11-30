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
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeFlush(uuid_t fileHandle)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               oserr;

    oserr = VFSNodeHandleGet(fileHandle, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // We only support the flush operation on regular files.
    if (!__NodeIsFile(handle->Node)) {
        oserr = OS_ENOTSUPPORTED;
        goto cleanup;
    }

    // TODO implement
    oserr = OS_ENOTSUPPORTED;

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}
