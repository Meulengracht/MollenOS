/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 *
 * Session Service (Protected) Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_SERVICES_SERVICE_H__
#define __DDK_SERVICES_SERVICE_H__

#include <ddk/ddkdefs.h>
#include <threads.h>

_CODE_BEGIN

#define SERVICE_SESSION_PATH "/service/session"
#define SERVICE_DEVICE_PATH "/service/device"
#define SERVICE_USB_PATH "/service/usb"
#define SERVICE_PROCESS_PATH "/service/process"
#define SERVICE_FILE_PATH "/service/file"
#define SERVICE_NET_PATH "/service/net"

// The ability to associate the current thread handle 
// with a global path to access services without knowing the thread
DDKDECL(oscode_t,
        RegisterPath(
    _In_ const char* Path));

// Service targets that are available for Vali
DDKDECL(thrd_t, GetSessionService(void));
DDKDECL(thrd_t, GetDeviceService(void));
DDKDECL(thrd_t, GetUsbService(void));
DDKDECL(thrd_t, GetProcessService(void));
DDKDECL(thrd_t, GetFileService(void));
DDKDECL(thrd_t, GetNetService(void));

// When a service is required for a module it is
// possible to wait for it to be available. Be careful
// about not giving a timeout
DDKDECL(oscode_t,
        WaitForSessionService(
    _In_ size_t Timeout));
DDKDECL(oscode_t,
        WaitForDeviceService(
    _In_ size_t Timeout));
DDKDECL(oscode_t,
        WaitForUsbService(
    _In_ size_t Timeout));
DDKDECL(oscode_t,
        WaitForProcessService(
    _In_ size_t Timeout));
DDKDECL(oscode_t,
        WaitForFileService(
    _In_ size_t Timeout));
DDKDECL(oscode_t,
        WaitForNetService(
    _In_ size_t Timeout));

_CODE_END

#endif //!__DDK_SERVICES_SERVICE_H__
