/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Session Service (Protected) Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_SERVICES_SESSION_H__
#define __DDK_SERVICES_SESSION_H__

#include <ddk/ddkdefs.h>
#include <os/types/session.h>

// IPC Function Declarations
#define __SESSIONMANAGER_REGISTER          (int)0
#define __SESSIONMANAGER_UNREGISTER        (int)1
#define __SESSIONMANAGER_LOOKUP			   (int)2
#define __SESSIONMANAGER_NEWDEVICE         (int)3
#define __SESSIONMANAGER_LOGIN             (int)4
#define __SESSIONMANAGER_LOGOUT            (int)5

_CODE_BEGIN
/* RegisterServiceObject
 * Registers a new service object with the session manager, which is then accessible
 * to the rest of the system for RPC activity. */
DDKDECL(OsStatus_t,
RegisterServiceObject(
	_In_  const char*           Name,
	_In_  ServiceCapabilities_t Capabilities,
	_In_  UUId_t                ChannelHandle,
	_Out_ UUId_t*               ServiceHandle));

/* UnregisterServiceObject
 * Registers a previously registered service handle from the system. */
DDKDECL(OsStatus_t,
UnregisterServiceObject(
	_In_ UUId_t ServiceHandle));

/* SessionCheckDisk
 * Notifies the sessionmanager if a new accessible system disk. */
DDKDECL(OsStatus_t,
SessionCheckDisk(
	_In_ const char* DiskIdentifier));
_CODE_END

#endif //!__SDK_SESSIONS_H__
