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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Session Service (Protected) Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/session.h>
#include <os/ipc.h>

OsStatus_t
SessionCheckDisk(
	_In_ const char* DiskIdentifier)
{
	thrd_t       ServiceTarget = GetSessionService();
	IpcMessage_t Request;

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __SESSIONMANAGER_NEWDEVICE);
	IpcSetUntypedArgument(&Request, 0, 
		(void*)DiskIdentifier, strlen(DiskIdentifier) + 1);
	
	return IpcInvoke(ServiceTarget, &Request,
		IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}
