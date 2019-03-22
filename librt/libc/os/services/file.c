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
 * File Service Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/file.h>
#include <os/services/file.h>
#include <os/services/targets.h>

FileSystemCode_t
OpenFile(
    _In_  const char*   Path, 
    _In_  Flags_t       Options, 
    _In_  Flags_t       Access,
    _Out_ UUId_t*       Handle)
{
    OpenFilePackage_t Package;
    MRemoteCall_t Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_OPEN);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetArgument(&Request, 1, (const void*)&Options, sizeof(Flags_t));
    RPCSetArgument(&Request, 2, (const void*)&Access, sizeof(Flags_t));
    RPCSetResult(&Request, (const void*)&Package, sizeof(OpenFilePackage_t));
    if (RPCExecute(&Request) != OsSuccess) {
        *Handle = UUID_INVALID;
        return FsInvalidParameters;
    }
    *Handle = Package.Handle;
    return Package.Code;
}

FileSystemCode_t
CloseFile(
    _In_ UUId_t Handle)
{
    FileSystemCode_t Result = FsOk;
    MRemoteCall_t    Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_CLOSE);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Result, sizeof(FileSystemCode_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return FsInvalidParameters;
    }
    return Result;
}

FileSystemCode_t
DeletePath(
    _In_ const char*    Path,
    _In_ Flags_t        Flags)
{
    FileSystemCode_t Result = FsOk;
    MRemoteCall_t    Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_DELETEPATH);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetArgument(&Request, 1, (const void*)&Flags, sizeof(Flags_t));
    RPCSetResult(&Request, (const void*)&Result, sizeof(FileSystemCode_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return FsInvalidParameters;
    }
    return Result;
}

FileSystemCode_t
ReadFile(
    _In_      UUId_t            Handle,
    _In_      UUId_t            BufferHandle,
    _In_      size_t            Length,
    _Out_Opt_ size_t*           BytesIndex,
    _Out_Opt_ size_t*           BytesRead)
{
    RWFilePackage_t Package;
    MRemoteCall_t   Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_READ);
    RPCSetArgument(&Request, 0, (const void*)&Handle,       sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)&BufferHandle, sizeof(UUId_t));
    RPCSetArgument(&Request, 2, (const void*)&Length,       sizeof(size_t));
    RPCSetResult(&Request,      (const void*)&Package,      sizeof(RWFilePackage_t));
    if (RPCExecute(&Request) != OsSuccess) {
        Package.ActualSize  = 0;
        Package.Index       = 0;
        Package.Code        = FsInvalidParameters;
    }

    if (BytesRead != NULL) {
        *BytesRead = Package.ActualSize;
    }
    if (BytesIndex != NULL) {
        *BytesIndex = Package.Index;
    }
    return Package.Code;
}

FileSystemCode_t
WriteFile(
    _In_      UUId_t            Handle,
    _In_      UUId_t            BufferHandle,
    _In_      size_t            Length,
    _Out_Opt_ size_t*           BytesWritten)
{
    RWFilePackage_t Package;
    MRemoteCall_t   Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_WRITE);
    RPCSetArgument(&Request, 0, (const void*)&Handle,       sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)&BufferHandle, sizeof(UUId_t));
    RPCSetArgument(&Request, 2, (const void*)&Length,       sizeof(size_t));
    RPCSetResult(&Request,      (const void*)&Package,      sizeof(RWFilePackage_t));
    if (RPCExecute(&Request) != OsSuccess) {
        Package.ActualSize  = 0;
        Package.Index       = 0;
        Package.Code        = FsInvalidParameters;
    }

    if (BytesWritten != NULL) {
        *BytesWritten = Package.ActualSize;
    }
    return Package.Code;
}

FileSystemCode_t
SeekFile(
    _In_ UUId_t     Handle, 
    _In_ uint32_t   SeekLo, 
    _In_ uint32_t   SeekHi)
{
    FileSystemCode_t Result = FsOk;
    MRemoteCall_t    Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_SEEK);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)&SeekLo, sizeof(uint32_t));
    RPCSetArgument(&Request, 2, (const void*)&SeekHi, sizeof(uint32_t));
    RPCSetResult(&Request,      (const void*)&Result, sizeof(FileSystemCode_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return FsInvalidParameters;
    }
    return Result;
}

FileSystemCode_t
FlushFile(
    _In_ UUId_t Handle)
{
    FileSystemCode_t Result = FsOk;
    MRemoteCall_t    Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_FLUSH);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Result, sizeof(FileSystemCode_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return FsInvalidParameters;
    }
    return Result;
}

FileSystemCode_t
MoveFile(
    _In_ const char*    Source,
    _In_ const char*    Destination,
    _In_ int            Copy)
{
    FileSystemCode_t Result = FsOk;
    MRemoteCall_t    Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_MOVE);
    RPCSetArgument(&Request, 0, (const void*)Source, strlen(Source) + 1);
    RPCSetArgument(&Request, 1, (const void*)Destination, strlen(Destination) + 1);
    RPCSetArgument(&Request, 2, (const void*)&Copy, sizeof(int));
    RPCSetResult(&Request, (const void*)&Result, sizeof(FileSystemCode_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return FsInvalidParameters;
    }
    return Result;
}

OsStatus_t
GetFilePosition(
    _In_      UUId_t    Handle,
    _Out_     uint32_t* PositionLo,
    _Out_Opt_ uint32_t* PositionHi)
{
    QueryFileValuePackage_t Package;
    MRemoteCall_t           Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_GETPOSITION);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Package, sizeof(QueryFileValuePackage_t));
    if (RPCExecute(&Request) != OsSuccess) {
        *PositionLo = 0;
        if (PositionHi != NULL) {
            *PositionHi = 0;
        }
        return OsError;
    }

    *PositionLo = Package.Value.Parts.Lo;
    if (PositionHi != NULL) {
        *PositionHi = Package.Value.Parts.Hi;
    }
    return OsSuccess;
}

OsStatus_t
GetFileOptions(
    _In_  UUId_t    Handle,
    _Out_ Flags_t*  Options,
    _Out_ Flags_t*  Access)
{
    QueryFileOptionsPackage_t Package;
    MRemoteCall_t             Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_GETOPTIONS);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Package, sizeof(QueryFileOptionsPackage_t));
    if (RPCExecute(&Request) != OsSuccess) {
        *Options = 0;
        *Access = 0;
        return OsError;
    }

    *Options = Package.Options;
    *Access = Package.Access;
    return OsSuccess;
}

OsStatus_t
SetFileOptions(
    _In_ UUId_t  Handle,
    _In_ Flags_t Options,
    _In_ Flags_t Access)
{
    MRemoteCall_t Request;
    OsStatus_t    Result = OsSuccess;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_SETOPTIONS);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)&Options, sizeof(Flags_t));
    RPCSetArgument(&Request, 2, (const void*)&Access, sizeof(Flags_t));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return OsError;
    }
    return Result;
}

OsStatus_t
GetFileSize(
    _In_ UUId_t         Handle,
    _Out_ uint32_t*     SizeLo,
    _Out_Opt_ uint32_t* SizeHi)
{
    QueryFileValuePackage_t Package;
    MRemoteCall_t           Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_GETSIZE);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Package, sizeof(QueryFileValuePackage_t));
    if (RPCExecute(&Request) != OsSuccess) {
        *SizeLo = 0;
        if (SizeHi != NULL) {
            *SizeHi = 0;
        }
        return OsError;
    }

    *SizeLo = Package.Value.Parts.Lo;
    if (SizeHi != NULL) {
        *SizeHi = Package.Value.Parts.Hi;
    }
    return OsSuccess;
}

OsStatus_t
GetFilePath(
    _In_  UUId_t    Handle,
    _Out_ char*     Path,
    _Out_ size_t    MaxLength)
{
    MRemoteCall_t Request;
    char          Buffer[_MAXPATH];

    memset(&Buffer[0], 0, _MAXPATH);

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_GETPATH);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&Buffer[0], _MAXPATH);
    if (RPCExecute(&Request) != OsSuccess) {
        return OsError;
    }
    memcpy(Path, &Buffer[0], MIN(MaxLength, strlen(&Buffer[0])));
    return OsSuccess;
}

FileSystemCode_t
GetFileStatsByPath(
    _In_ const char*            Path,
    _In_ OsFileDescriptor_t*    FileDescriptor)
{
    QueryFileStatsPackage_t Package;
    MRemoteCall_t           Request;
    FileSystemCode_t        Status = FsInvalidParameters;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_GETSTATSBYPATH);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetResult(&Request, (const void*)&Package, sizeof(QueryFileStatsPackage_t));
    
    if (RPCExecute(&Request) == OsSuccess) {
        Status = Package.Code;
        memcpy((void*)FileDescriptor, &Package.Descriptor, sizeof(OsFileDescriptor_t));
    }
    return Status;
}

FileSystemCode_t
GetFileStatsByHandle(
    _In_ UUId_t                 Handle,
    _In_ OsFileDescriptor_t*    FileDescriptor)
{
    QueryFileStatsPackage_t Package;
    MRemoteCall_t           Request;
    FileSystemCode_t        Status = FsInvalidParameters;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_GETSTATSBYHANDLE);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)&FileDescriptor, sizeof(OsFileDescriptor_t));
    
    if (RPCExecute(&Request) == OsSuccess) {
        Status = Package.Code;
        memcpy((void*)FileDescriptor, &Package.Descriptor, sizeof(OsFileDescriptor_t));
    }
    return Status;
}

FileSystemCode_t
GetFileSystemStatsByPath(
    _In_ const char*               Path,
    _In_ OsFileSystemDescriptor_t* FileDescriptor)
{
    
}

FileSystemCode_t
GetFileSystemStatsByHandle(
    _In_ UUId_t                    Handle,
    _In_ OsFileSystemDescriptor_t* FileDescriptor)
{
    
}
