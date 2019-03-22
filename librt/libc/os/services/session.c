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
 * Session Service Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/session.h>
#include <os/services/session.h>
#include <os/services/targets.h>

OsStatus_t
GetServiceObjectsWithCapabilities(
	_In_ ServiceCapabilities_t Capabilities,
	_In_ ServiceObject_t*      ObjectBuffer,
	_In_ size_t                MaxObjects)
{
	
}

OsStatus_t
SessionLoginRequest(
	_In_ const char*      User,
    _In_ const char*      Password,
    _In_ SessionObject_t* Result)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOGIN);
	RPCSetArgument(&Request, 0, (const void*)User, strlen(User) + 1);
    RPCSetArgument(&Request, 1, (const void*)Password, strlen(Password) + 1);
    RPCSetResult(&Request, (const void*)Result, sizeof(SessionObject_t));
	return RPCExecute(&Request);
}

OsStatus_t
SessionLogoutRequest(
	_In_ const char* SessionId)
{
	MRemoteCall_t Request;
    OsStatus_t    Result = OsError;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOGOUT);
	RPCSetArgument(&Request, 0, (const void*)SessionId, strlen(SessionId) + 1);
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
	if (RPCExecute(&Request) != OsSuccess) {
        return OsError;
    }
    return Result;
}
