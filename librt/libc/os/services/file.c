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
 * File Service Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/file.h>
#include <os/services/file.h>
#include <os/services/process.h>
#include <os/ipc.h>

FileSystemCode_t
OpenFile(
    _In_  const char*   Path, 
    _In_  Flags_t       Options, 
    _In_  Flags_t       Access,
    _Out_ UUId_t*       Handle)
{
	thrd_t             ServiceTarget = GetFileService();
	IpcMessage_t       Request;
	OsStatus_t         Status;
    OpenFilePackage_t* Package;
	
	if (!Path || !Handle) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_OPEN);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Options);
	IPC_SET_TYPED(&Request, 3, Access);
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
	    return FsInvalidParameters;
	}
	
    *Handle = Package->Handle;
    return Package->Code;
}

FileSystemCode_t
CloseFile(
    _In_ UUId_t Handle)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_CLOSE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return FsInvalidParameters;
	}
	return IPC_CAST_AND_DEREF(Result, FileSystemCode_t);
}

FileSystemCode_t
DeletePath(
    _In_ const char*    Path,
    _In_ Flags_t        Flags)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Path) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_DELETEPATH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Flags);
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return FsInvalidParameters;
	}
	return IPC_CAST_AND_DEREF(Result, FileSystemCode_t);
}

FileSystemCode_t
TransferFile(
    _In_      UUId_t  Handle,
    _In_      UUId_t  BufferHandle,
    _In_      int     Direction,
    _In_      size_t  Offset,
    _In_      size_t  Length,
    _Out_Opt_ size_t* BytesTransferred)
{
	thrd_t           ServiceTarget = GetFileService();
	IpcMessage_t     Request;
	OsStatus_t       Status;
    RWFilePackage_t* Package;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, (Direction == 0) ? __FILEMANAGER_READ : __FILEMANAGER_WRITE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, BufferHandle);
	IPC_SET_TYPED(&Request, 4, Offset);
	IpcSetUntypedArgument(&Request, 5, &Length, sizeof(size_t));
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        Package->ActualSize = 0;
        Package->Code       = FsInvalidParameters;
	}
	
    if (BytesTransferred != NULL) {
        *BytesTransferred = Package->ActualSize;
    }
    return Package->Code;
}

FileSystemCode_t
SeekFile(
    _In_ UUId_t     Handle, 
    _In_ uint32_t   SeekLo, 
    _In_ uint32_t   SeekHi)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_SEEK);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, SeekLo);
	IPC_SET_TYPED(&Request, 4, SeekHi);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return FsInvalidParameters;
	}
	return IPC_CAST_AND_DEREF(Result, FileSystemCode_t);
}

FileSystemCode_t
FlushFile(
    _In_ UUId_t Handle)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_FLUSH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return FsInvalidParameters;
	}
	return IPC_CAST_AND_DEREF(Result, FileSystemCode_t);
}

FileSystemCode_t
MoveFile(
    _In_ const char*    Source,
    _In_ const char*    Destination,
    _In_ int            Copy)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Source || !Destination) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_MOVE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Copy);
	IPC_SET_UNTYPED_STRING(&Request, 0, Source);
	IPC_SET_UNTYPED_STRING(&Request, 1, Destination);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return FsInvalidParameters;
	}
	return IPC_CAST_AND_DEREF(Result, FileSystemCode_t);
}

OsStatus_t
GetFilePosition(
    _In_      UUId_t    Handle,
    _Out_     uint32_t* PositionLo,
    _Out_Opt_ uint32_t* PositionHi)
{
	thrd_t                   ServiceTarget = GetFileService();
	IpcMessage_t             Request;
	OsStatus_t               Status;
    QueryFileValuePackage_t* Package;
	
	if (!PositionLo) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_GETPOSITION);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return Status;
	}

    *PositionLo = Package->Value.Parts.Lo;
    if (PositionHi != NULL) {
        *PositionHi = Package->Value.Parts.Hi;
    }
    return OsSuccess;
}

OsStatus_t
GetFileOptions(
    _In_  UUId_t    Handle,
    _Out_ Flags_t*  Options,
    _Out_ Flags_t*  Access)
{
	thrd_t                     ServiceTarget = GetFileService();
	IpcMessage_t               Request;
	OsStatus_t                 Status;
    QueryFileOptionsPackage_t* Package;
	
	if (!Options || !Access) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_GETOPTIONS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return Status;
	}
    
    *Options = Package->Options;
    *Access  = Package->Access;
    return OsSuccess;
}

OsStatus_t
SetFileOptions(
    _In_ UUId_t  Handle,
    _In_ Flags_t Options,
    _In_ Flags_t Access)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_SETOPTIONS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, Options);
	IPC_SET_TYPED(&Request, 4, Access);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
GetFileSize(
    _In_ UUId_t         Handle,
    _Out_ uint32_t*     SizeLo,
    _Out_Opt_ uint32_t* SizeHi)
{
	thrd_t                   ServiceTarget = GetFileService();
	IpcMessage_t             Request;
	OsStatus_t               Status;
    QueryFileValuePackage_t* Package;
	
	if (!SizeLo) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_GETSIZE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return Status;
	}

    *SizeLo = Package->Value.Parts.Lo;
    if (SizeHi != NULL) {
        *SizeHi = Package->Value.Parts.Hi;
    }
    return OsSuccess;
}

OsStatus_t
GetFilePath(
    _In_ UUId_t Handle,
    _In_ char*  Path,
    _In_ size_t MaxLength)
{
	thrd_t       ServiceTarget = GetFileService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	char*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_GETPATH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
    memcpy(Path, &Result[0], MIN(MaxLength, strlen(&Result[0])));
    return OsSuccess;
}

FileSystemCode_t
GetFileStatsByPath(
    _In_ const char*         Path,
    _In_ OsFileDescriptor_t* FileDescriptor)
{
	thrd_t                   ServiceTarget = GetFileService();
	IpcMessage_t             Request;
	OsStatus_t               Status;
    QueryFileStatsPackage_t* Package;
	
	if (!Path || !FileDescriptor) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_GETSTATSBYPATH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return FsInvalidParameters;
	}
	
    memcpy(FileDescriptor, &Package->Descriptor, sizeof(OsFileDescriptor_t));
    return Package->Code;
}

FileSystemCode_t
GetFileStatsByHandle(
    _In_ UUId_t              Handle,
    _In_ OsFileDescriptor_t* FileDescriptor)
{
	thrd_t                   ServiceTarget = GetFileService();
	IpcMessage_t             Request;
	OsStatus_t               Status;
    QueryFileStatsPackage_t* Package;
	
	if (!FileDescriptor) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_GETSTATSBYHANDLE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return FsInvalidParameters;
	}
	
    memcpy(FileDescriptor, &Package->Descriptor, sizeof(OsFileDescriptor_t));
    return Package->Code;
}

FileSystemCode_t
GetFileSystemStatsByPath(
    _In_ const char*               Path,
    _In_ OsFileSystemDescriptor_t* Descriptor)
{
    QueryFileSystemStatsPackage_t* Package;
	thrd_t                         ServiceTarget = GetFileService();
	IpcMessage_t                   Request;
	OsStatus_t                     Status;
	
	if (!Path || !Descriptor) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_QUERY_FILESYSTEM_BY_PATH);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return FsInvalidParameters;
	}
	
    memcpy(Descriptor, &Package->Descriptor, sizeof(OsFileSystemDescriptor_t));
    return Package->Code;
}

FileSystemCode_t
GetFileSystemStatsByHandle(
    _In_ UUId_t                    Handle,
    _In_ OsFileSystemDescriptor_t* Descriptor)
{
    QueryFileSystemStatsPackage_t* Package;
	thrd_t                         ServiceTarget = GetFileService();
	IpcMessage_t                   Request;
	OsStatus_t                     Status;
	
	if (!Descriptor) {
	    return FsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __FILEMANAGER_QUERY_FILESYSTEM_BY_HANDLE);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
        return FsInvalidParameters;
	}
	
    memcpy(Descriptor, &Package->Descriptor, sizeof(OsFileSystemDescriptor_t));
    return Package->Code;
}
