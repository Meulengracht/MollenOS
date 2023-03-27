/**
 * Copyright 2017, Philip Meulengracht
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

#include <errno.h>
#include <ddk/service.h>
#include <os/device.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>

#include <sys_device_service_client.h>

oserr_t
OSDeviceIOCtl(
        _In_ uuid_t              deviceId,
        _In_ enum OSIOCtlRequest request,
        _In_ void*               buffer,
        _In_ size_t              length)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t                  oserr;
    uint8_t*                 u8 = buffer;
    int                      status;

    status = sys_device_ioctl(
            GetGrachtClient(),
            &msg.base,
            deviceId,
            (unsigned int)request,
            u8,
            length
    );
    if (status) {
        return OsErrToErrNo(status);
    }

    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_device_ioctl_result(
            GetGrachtClient(),
            &msg.base,
            u8,
            length,
            &oserr
    );
    return oserr;
}
