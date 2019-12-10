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
 * Path Service Definitions & Structures
 * - This header describes the base path-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/file.h>
#include <os/services/path.h>
#include <os/services/process.h>
#include <os/ipc.h>

OsStatus_t
PathResolveEnvironment(
    _In_ EnvironmentPath_t Base,
    _In_ char*             Buffer,
    _In_ size_t            MaxLength)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
    char*        Result;
	
	if (!Buffer) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_PATHRESOLVE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Base);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Buffer, Result, MIN(MaxLength, strlen(&Result[0])));
	return OsSuccess;
}

OsStatus_t
PathCanonicalize(
    _In_ const char* Path,
    _In_ char*       Buffer,
    _In_ size_t      MaxLength)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
    char*        Result;
	
	if (!Path || !Buffer) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_PATHCANONICALIZE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Buffer, Result, MIN(MaxLength, strlen(&Result[0])));
	return OsSuccess;
}
