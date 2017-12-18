/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - File Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _FILE_INTERFACE_H_
#define _FILE_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <stddef.h>

/* Includes
 * - System */
#include <os/driver/file/definitions.h>
#include <os/driver/service.h>
#include <os/driver/buffer.h>
#include <os/ipc/ipc.h>

/* This is the options structure used exclusively
 * for multi-params readback from the ipc operations */
PACKED_TYPESTRUCT(OpenFilePackage, {
	FileSystemCode_t		Code;
	UUId_t					Handle;
});
PACKED_TYPESTRUCT(RWFilePackage, {
	FileSystemCode_t		Code;
	size_t					Index;
	size_t					ActualSize;
});
PACKED_TYPESTRUCT(QueryFileValuePackage, {
	FileSystemCode_t		Code;
	union {
		struct {
			uint32_t Lo;
			uint32_t Hi;
		} Parts;
		uint64_t Full;
	} Value;
});
PACKED_TYPESTRUCT(QueryFileOptionsPackage, {
	FileSystemCode_t		Code;
	Flags_t					Options;
	Flags_t					Access;
});

/* These definitions are in-place to allow a custom
 * setting of the file-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __FILEMANAGER_INTERFACE_VERSION		1

/* These are the different IPC functions supported
 * by the filemanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __FILEMANAGER_REGISTERDISK				IPC_DECL_FUNCTION(0)
#define __FILEMANAGER_UNREGISTERDISK			IPC_DECL_FUNCTION(1)
#define __FILEMANAGER_QUERYDISKS				IPC_DECL_FUNCTION(2)

#define __FILEMANAGER_OPENFILE					IPC_DECL_FUNCTION(3)
#define __FILEMANAGER_CLOSEFILE					IPC_DECL_FUNCTION(4)
#define __FILEMANAGER_DELETEFILE				IPC_DECL_FUNCTION(5)
#define __FILEMANAGER_READFILE					IPC_DECL_FUNCTION(6)
#define __FILEMANAGER_WRITEFILE					IPC_DECL_FUNCTION(7)
#define __FILEMANAGER_SEEKFILE					IPC_DECL_FUNCTION(8)
#define __FILEMANAGER_FLUSHFILE					IPC_DECL_FUNCTION(9)
#define __FILEMANAGER_MOVEFILE					IPC_DECL_FUNCTION(10)

#define __FILEMANAGER_GETPOSITION				IPC_DECL_FUNCTION(11)
#define __FILEMANAGER_GETOPTIONS				IPC_DECL_FUNCTION(12)
#define __FILEMANAGER_SETOPTIONS				IPC_DECL_FUNCTION(13)
#define __FILEMANAGER_GETSIZE					IPC_DECL_FUNCTION(14)
#define __FILEMANAGER_GETPATH					IPC_DECL_FUNCTION(15)

#define __FILEMANAGER_PATHRESOLVE				IPC_DECL_FUNCTION(16)
#define __FILEMANAGER_PATHCANONICALIZE			IPC_DECL_FUNCTION(17)

/* Bit flag defintions for operations such as 
 * registering / unregistering of disks, open flags
 * for OpenFile and access flags */
#define __DISK_REMOVABLE						0x00000001

#define __DISK_FORCED_REMOVE					0x00000001

// Access flags
#define __FILE_READ_ACCESS						0x00000001
#define __FILE_WRITE_ACCESS						0x00000002
#define __FILE_READ_SHARE						0x00000100
#define __FILE_WRITE_SHARE						0x00000200

// Options flag
#define __FILE_CREATE							0x00000001
#define __FILE_TRUNCATE							0x00000002
#define __FILE_MUSTEXIST						0x00000004
#define __FILE_FAILONEXIST						0x00000008
#define __FILE_APPEND							0x00000100
#define __FILE_BINARY							0x00000200
#define __FILE_VOLATILE							0x00000400
#define __FILE_TEMPORARY						0x00000800

/* RegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
OsStatus_t
SERVICEABI
RegisterDisk(
	_In_ UUId_t Driver, 
	_In_ UUId_t Device, 
	_In_ Flags_t Flags);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
RegisterDisk( 
	_In_ UUId_t Device, 
	_In_ Flags_t Flags)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize RPC
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_REGISTERDISK);

	// Setup arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Flags, sizeof(Flags_t));

	// Send event, no response
	return RPCEvent(&Request, __FILEMANAGER_TARGET);
}
#endif

/* UnregisterDisk
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
OsStatus_t
SERVICEABI
UnregisterDisk(
	_In_ UUId_t Device, 
	_In_ Flags_t Flags);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UnregisterDisk(
	_In_ UUId_t Device,
	_In_ Flags_t Flags)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize RPC
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_UNREGISTERDISK);

	// Setup arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Flags, sizeof(Flags_t));

	// Send event, no response
	return RPCEvent(&Request, __FILEMANAGER_TARGET);
}
#endif

/* OpenFile
 * Opens or creates the given file path based on
 * the given <Access> and <Options> flags. See the
 * top of this file */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SERVICEABI
OpenFile(
	_In_ UUId_t Requester,
	_In_ __CONST char *Path, 
	_In_ Flags_t Options, 
	_In_ Flags_t Access,
	_Out_ UUId_t *Handle);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
OpenFile(
	_In_ __CONST char *Path, 
	_In_ Flags_t Options, 
	_In_ Flags_t Access,
	_Out_ UUId_t *Handle)
{
	// Variables
	OpenFilePackage_t Package;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_OPENFILE);

	// Set arguments
	RPCSetArgument(&Request, 0, (__CONST void*)Path, strlen(Path));
	RPCSetArgument(&Request, 1, (__CONST void*)&Options, sizeof(Flags_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&Access, sizeof(Flags_t));

	// Set result buffer
	RPCSetResult(&Request, (__CONST void*)&Package, sizeof(OpenFilePackage_t));

	// Execute the rpc request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		*Handle = UUID_INVALID;
		return FsInvalidParameters;
	}

	// Update out variables
	*Handle = Package.Handle;

	// Return the result code
	return Package.Code;
}
#endif

/* CloseFile
 * Closes the given file-handle, but does not necessarily
 * close the link to the file. Returns the result */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SERVICEABI
CloseFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
CloseFile(
	_In_ UUId_t Handle)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_CLOSEFILE);

	// Set request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));

	// Set result buffer
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(FileSystemCode_t));

	// Execute the request 
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return FsInvalidParameters;
	}

	// Return the result code
	return Result;
}
#endif

/* DeleteFile
 * Deletes the given file associated with the filehandle
 * the caller must make sure there is no other references
 * to the file - otherwise delete fails */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SERVICEABI
DeleteFile(
	_In_ UUId_t Requester, 
	_In_ __CONST char *Path);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
DeleteFile(
	_In_ __CONST char *Path)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_DELETEFILE);

	// Set request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)Path, strlen(Path));

	// Set result buffer
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(FileSystemCode_t));

	// Execute the request 
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return FsInvalidParameters;
	}

	// Return the result code
	return Result;
}
#endif

/* ReadFile
 * Reads the requested number of bytes into the given buffer
 * from the current position in the file-handle */
#ifdef __FILEMANAGER_IMPL
__EXTERN
FileSystemCode_t
SERVICEABI
ReadFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesIndex,
	_Out_ size_t *BytesRead);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
ReadFile(
	_In_ UUId_t Handle, 
	_Out_ BufferObject_t *BufferObject,
	_Out_Opt_ size_t *BytesIndex,
	_Out_Opt_ size_t *BytesRead)
{
	// Variables
	RWFilePackage_t Package;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_READFILE);

	// Set request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)BufferObject, GetBufferObjectSize(BufferObject));

	// Set result buffer
	RPCSetResult(&Request, (__CONST void*)&Package, sizeof(RWFilePackage_t));

	// Execute the request 
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		Package.ActualSize = 0;
		Package.Index = 0;
		Package.Code = FsInvalidParameters;
	}

	// Update out's
	if (BytesRead != NULL) {
		*BytesRead = Package.ActualSize;
	}
	if (BytesIndex != NULL) {
		*BytesIndex = Package.Index;
	}
	
	// Return the result code
	return Package.Code;
}
#endif

/* WriteFile
 * Writes the requested number of bytes from the given buffer
 * into the current position in the file-handle */
#ifdef __FILEMANAGER_IMPL
__EXTERN
FileSystemCode_t
SERVICEABI
WriteFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_In_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesWritten);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
WriteFile(
	_In_ UUId_t Handle,
	_In_ BufferObject_t *BufferObject,
	_Out_Opt_ size_t *BytesWritten)
{
	// Variables
	RWFilePackage_t Package;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_WRITEFILE);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)BufferObject, GetBufferObjectSize(BufferObject));

	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Package, sizeof(RWFilePackage_t));

	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		Package.ActualSize = 0;
		Package.Index = 0;
		Package.Code = FsInvalidParameters;
	}

	// Update out's
	if (BytesWritten != NULL) {
		*BytesWritten = Package.ActualSize;
	}

	// Return the result code
	return Package.Code;
}
#endif

/* SeekFile
 * Sets the file-pointer for the given handle to the
 * values given, the position is absolute and must
 * be within range of the file size */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SERVICEABI
SeekFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle, 
	_In_ uint32_t SeekLo, 
	_In_ uint32_t SeekHi);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
SeekFile(
	_In_ UUId_t Handle, 
	_In_ uint32_t SeekLo, 
	_In_ uint32_t SeekHi)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_SEEKFILE);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&SeekLo, sizeof(uint32_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&SeekHi, sizeof(uint32_t));

	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(FileSystemCode_t));

	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return FsInvalidParameters;
	}

	// Return the result code
	return Result;
}
#endif

/* FlushFile
 * Flushes the internal file buffers and ensures there are
 * no pending file operations for the given file handle */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SERVICEABI
FlushFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
FlushFile(
	_In_ UUId_t Handle)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_FLUSHFILE);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));

	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(FileSystemCode_t));

	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return FsInvalidParameters;
	}

	// Return the result code
	return Result;
}
#endif

/* MoveFile
 * Moves or copies a given file path to the destination path
 * this can also be used for renamining if the dest/source paths
 * match (except for filename/directoryname) */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SERVICEABI
MoveFile(
	_In_ UUId_t Requester,
	_In_ __CONST char *Source, 
	_In_ __CONST char *Destination,
	_In_ int Copy);
#else
SERVICEAPI
FileSystemCode_t
SERVICEABI
MoveFile(
	_In_ __CONST char *Source,
	_In_ __CONST char *Destination,
	_In_ int Copy)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_MOVEFILE);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)Source, strlen(Source));
	RPCSetArgument(&Request, 1, (__CONST void*)Destination, strlen(Destination));
	RPCSetArgument(&Request, 2, (__CONST void*)&Copy, sizeof(int));
	
	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(FileSystemCode_t));
	
	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return FsInvalidParameters;
	}

	// Return the result code
	return Result;
}
#endif

/* GetFilePosition 
 * Queries the current file position that the given handle
 * is at, it returns as two separate unsigned values, the upper
 * value is optional and should only be checked for large files */
#ifdef __FILEMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
GetFilePosition(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ QueryFileValuePackage_t *Result);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
GetFilePosition(
	_In_ UUId_t Handle,
	_Out_ uint32_t *PositionLo,
	_Out_Opt_ uint32_t *PositionHi)
{
	// Variables
	QueryFileValuePackage_t Package;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_GETPOSITION);
	
	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));
	
	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Package, sizeof(QueryFileValuePackage_t));
	
	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		*PositionLo = 0;
		if (PositionHi != NULL) {
			*PositionHi = 0;
		}
		return OsError;
	}

	// Update out's
	*PositionLo = Package.Value.Parts.Lo;
	if (PositionHi != NULL) {
		*PositionHi = Package.Value.Parts.Hi;
	}

	// Return the result code
	return OsSuccess;
}
#endif

/* GetFileOptions 
 * Queries the current file options and file access flags
 * for the given file handle */
#ifdef __FILEMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
GetFileOptions(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ QueryFileOptionsPackage_t *Result);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
GetFileOptions(
	_In_ UUId_t Handle,
	_Out_ Flags_t *Options,
	_Out_ Flags_t *Access)
{
	// Variables
	QueryFileOptionsPackage_t Package;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_GETOPTIONS);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));
	
	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Package, sizeof(QueryFileOptionsPackage_t));
	
	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		*Options = 0;
		*Access = 0;
		return OsError;
	}

	// Update out's
	*Options = Package.Options;
	*Access = Package.Access;
	
	// Return no error
	return OsSuccess;
}
#endif

/* SetFileOptions 
 * Attempts to modify the current option and or access flags
 * for the given file handle as specified by <Options> and <Access> */
#ifdef __FILEMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
SetFileOptions(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_In_ Flags_t Options,
	_In_ Flags_t Access);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
SetFileOptions(
	_In_ UUId_t Handle,
	_In_ Flags_t Options,
	_In_ Flags_t Access)
{
	// Variables
	MRemoteCall_t Request;
	OsStatus_t Result = OsSuccess;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_SETOPTIONS);
	
	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Options, sizeof(Flags_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&Access, sizeof(Flags_t));
	
	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(OsStatus_t));
	
	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return OsError;
	}

	// Return the result
	return Result;
}
#endif

/* GetFileSize 
 * Queries the current file size that the given handle
 * has, it returns as two separate unsigned values, the upper
 * value is optional and should only be checked for large files */
#ifdef __FILEMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
GetFileSize(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ QueryFileValuePackage_t *Result);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
GetFileSize(
	_In_ UUId_t Handle,
	_Out_ uint32_t *SizeLo,
	_Out_Opt_ uint32_t *SizeHi)
{
	// Variables
	QueryFileValuePackage_t Package;
	MRemoteCall_t Request;

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_GETSIZE);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));

	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Package, sizeof(QueryFileValuePackage_t));

	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		*SizeLo = 0;
		if (SizeHi != NULL) {
			*SizeHi = 0;
		}
		return OsError;
	}

	// Update out's
	*SizeLo = Package.Value.Parts.Lo;
	if (SizeHi != NULL) {
		*SizeHi = Package.Value.Parts.Hi;
	}

	// Return the result
	return OsSuccess;
}
#endif

/* GetFilePath 
 * Queries the path of a file that the given handle
 * has, it returns it as a UTF8 string with max length of _MAXPATH */
#ifdef __FILEMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
GetFilePath(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ __CONST char *Path);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
GetFilePath(
	_In_ UUId_t Handle,
	_Out_ char *Path,
	_Out_ size_t MaxLength)
{
	// Variables
	MRemoteCall_t Request;
	char Buffer[_MAXPATH];

	// Reset local buffer
	memset(&Buffer[0], 0, _MAXPATH);

	// Initialize the request
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_GETPATH);

	// Set the request arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, sizeof(UUId_t));

	// Set the request result buffer
	RPCSetResult(&Request, (__CONST void*)&Buffer[0], _MAXPATH);

	// Execute the request
	if (RPCExecute(&Request, __FILEMANAGER_TARGET) != OsSuccess) {
		return OsError;
	}

	// Copy the result into the given buffer
	memcpy(Path, &Buffer[0], MIN(MaxLength, strlen(&Buffer[0])));

	// Done
	return OsSuccess;
}
#endif

#endif //!_FILE_INTERFACE_H_
