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

#include <ddk/services/session.h>
#include <os/services/session.h>
#include <os/services/targets.h>

OsStatus_t
RegisterServiceObject(
	_In_  const char*           Name,
	_In_  ServiceCapabilities_t Capabilities,
	_In_  UUId_t                ChannelHandle,
	_Out_ UUId_t*               ServiceHandle)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_REGISTER);
	RPCSetArgument(&Request, 0, (const void*)Name, strlen(Name) + 1);
	RPCSetArgument(&Request, 1, (const void*)&Capabilities, sizeof(ServiceCapabilities_t));
    RPCSetArgument(&Request, 2, (const void*)&ChannelHandle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)ServiceHandle, sizeof(UUId_t));
	return RPCExecute(&Request);
}

OsStatus_t
UnregisterServiceObject(
	_In_ UUId_t ServiceHandle)
{
	MRemoteCall_t Request;
	OsStatus_t    Status = OsSuccess;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_UNREGISTER);
	RPCSetArgument(&Request, 0, (const void*)&ServiceHandle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Status, sizeof(OsStatus_t));
    RPCExecute(&Request);
	return Status;
}

OsStatus_t
SessionCheckDisk(
	_In_ const char* DiskIdentifier)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_NEWDEVICE);
	RPCSetArgument(&Request, 0, (const void*)DiskIdentifier, strlen(DiskIdentifier) + 1);
	return RPCEvent(&Request);
}
