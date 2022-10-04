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
 */

#include <ddk/convert.h>
#include <ds/list.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/services/mount.h>

#include <sys_mount_service_client.h>

oserr_t
Mount(
        _In_ const char*  path,
        _In_ const char*  at,
        _In_ const char*  type,
        _In_ unsigned int flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  status;

    sys_mount_mount(
            GetGrachtClient(),
            &msg.base,
            *__crt_processid_ptr(),
            path,
            at,
            type,
            flags
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_mount_mount_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

oserr_t
Unmount(
        _In_ const char* path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  status;

    sys_mount_unmount(
            GetGrachtClient(),
            &msg.base,
            *__crt_processid_ptr(),
            path
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_mount_unmount_result(GetGrachtClient(), &msg.base, &status);
    return status;
}
