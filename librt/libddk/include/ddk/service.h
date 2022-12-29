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

_CODE_BEGIN

// imported from gracht types.h, to avoid including that header
typedef struct gracht_server gracht_server_t;

struct ServiceStartupOptions {
    // Server is the handle to the gracht server itself. This is exposed to allow
    // services to register the protocols they support.
    gracht_server_t* Server;
    // ServerHandle is the handle of gracht server link. This is exposed to allow
    // services to share their handle with other procceses to allow communication.
    // While this is possible to discover with just the above Server handle, we add
    // it here for convenience.
    uuid_t ServerHandle;
};

// While we no longer hardcode these paths in the code when setting them, we still hardcode
// them when discovering. This means that we cannot remove these just yet, and instead they must
// match the service YAML paths.
#define SERVICE_SESSION_PATH "/service/session"
#define SERVICE_DEVICE_PATH "/service/device"
#define SERVICE_USB_PATH "/service/usb"
#define SERVICE_PROCESS_PATH "/service/process"
#define SERVICE_FILE_PATH "/service/file"
#define SERVICE_NET_PATH "/service/net"
#define SERVICE_SERVED_PATH "/service/serve"

// The ability to associate the current thread handle 
// with a global path to access services without knowing the thread
DDKDECL(oserr_t,
RegisterPath(
    _In_ const char* Path));

// Service targets that are available for Vali
DDKDECL(uuid_t, GetSessionService(void));
DDKDECL(uuid_t, GetDeviceService(void));
DDKDECL(uuid_t, GetUsbService(void));
DDKDECL(uuid_t, GetProcessService(void));
DDKDECL(uuid_t, GetFileService(void));
DDKDECL(uuid_t, GetNetService(void));

// When a service is required for a module it is
// possible to wait for it to be available. Be careful
// about not giving a timeout
DDKDECL(oserr_t,
WaitForSessionService(
    _In_ size_t Timeout));
DDKDECL(oserr_t,
WaitForDeviceService(
    _In_ size_t Timeout));
DDKDECL(oserr_t,
WaitForUsbService(
    _In_ size_t Timeout));
DDKDECL(oserr_t,
WaitForProcessService(
    _In_ size_t Timeout));
DDKDECL(oserr_t,
WaitForFileService(
    _In_ size_t Timeout));
DDKDECL(oserr_t,
WaitForNetService(
    _In_ size_t Timeout));

_CODE_END

#endif //!__DDK_SERVICES_SERVICE_H__
