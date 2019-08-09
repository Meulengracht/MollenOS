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
 * Storage Service Definitions & Structures
 * - This header describes the base storage-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/file.h>
#include <os/services/storage.h>
#include <os/services/process.h>
#include <os/ipc.h>
#include <string.h>

OsStatus_t
QueryStorageByPath(
    _In_ const char*            Path,
    _In_ OsStorageDescriptor_t* StorageDescriptor)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Path || !StorageDescriptor) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_QUERYSTORAGEBYPATH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
    memcpy(StorageDescriptor, Result, sizeof(OsStorageDescriptor_t));
    return OsSuccess;
}

OsStatus_t
QueryStorageByHandle(
    _In_ UUId_t                 Handle,
    _In_ OsStorageDescriptor_t* StorageDescriptor)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!StorageDescriptor) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_QUERYSTORAGEBYPATH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
    memcpy(StorageDescriptor, Result, sizeof(OsStorageDescriptor_t));
    return OsSuccess;
}
