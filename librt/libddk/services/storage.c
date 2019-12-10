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
 * Storage Service (Protected) Definitions & Structures
 * - This header describes the base storage-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/file.h>
#include <os/ipc.h>
#include <os/services/process.h>

OsStatus_t
RegisterStorage( 
    _In_ UUId_t  DeviceId, 
    _In_ Flags_t Flags)
{
	IpcMessage_t Request;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_REGISTERSTORAGE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, DeviceId);
	IPC_SET_TYPED(&Request, 3, Flags);
	
	return IpcInvoke(GetFileService(), &Request, 
	    IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}

OsStatus_t
UnregisterStorage(
    _In_ UUId_t  DeviceId,
    _In_ Flags_t Flags)
{
	IpcMessage_t Request;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_UNREGISTERSTORAGE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, DeviceId);
	IPC_SET_TYPED(&Request, 3, Flags);
	
	return IpcInvoke(GetFileService(), &Request, 
	    IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}
