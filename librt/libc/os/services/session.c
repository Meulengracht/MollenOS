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
 * Session Service Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/session.h>
#include <os/services/session.h>
#include <os/services/process.h>
#include <os/ipc.h>
#include <string.h>

OsStatus_t
SessionLoginRequest(
	_In_ const char*      User,
    _In_ const char*      Password,
    _In_ SessionObject_t* Result)
{
	thrd_t       ServiceTarget = GetSessionService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Buffer;
	
	if (!User || !Password || !Result) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __SESSIONMANAGER_LOGIN);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, User);
	IPC_SET_UNTYPED_STRING(&Request, 1, Password);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Buffer);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Result, Buffer, sizeof(SessionObject_t));
	return OsSuccess;
}

OsStatus_t
SessionLogoutRequest(
	_In_ const char* SessionId)
{
	thrd_t       ServiceTarget = GetSessionService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Buffer;
	
	if (!SessionId) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __SESSIONMANAGER_LOGOUT);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, SessionId);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Buffer);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Buffer, OsStatus_t);
}
