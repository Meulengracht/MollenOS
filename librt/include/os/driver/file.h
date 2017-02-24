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
 * - System */
#include <os/osdefs.h>
#include <os/driver/file/definitions.h>
#include <os/driver/server.h>
#include <os/driver/buffer.h>
#include <os/ipc/ipc.h>

/* This is the options structure used exclusively
 * for multi-params readback from the ipc operations */
PACKED_TYPESTRUCT(OpenFilePackage, {
	FileSystemCode_t		Code;
	UUId_t					Handle;
});
PACKED_TYPESTRUCT(ReadWriteFilePackage, {
	FileSystemCode_t		Code;
	size_t					ActualSize;
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

#define __FILEMANAGER_OPENFILE					IPC_DECL_FUNCTION(2)
#define __FILEMANAGER_CLOSEFILE					IPC_DECL_FUNCTION(3)
#define __FILEMANAGER_DELETEFILE				IPC_DECL_FUNCTION(4)
#define __FILEMANAGER_READFILE					IPC_DECL_FUNCTION(5)
#define __FILEMANAGER_WRITEFILE					IPC_DECL_FUNCTION(6)
#define __FILEMANAGER_SEEKFILE					IPC_DECL_FUNCTION(7)
#define __FILEMANAGER_FLUSHFILE					IPC_DECL_FUNCTION(8)
#define __FILEMANAGER_MOVEFILE					IPC_DECL_FUNCTION(9)

#define __FILEMANAGER_PATHRESOLVE				IPC_DECL_FUNCTION(10)
#define __FILEMANAGER_PATHCANONICALIZE			IPC_DECL_FUNCTION(11)

/* Bit flag defintions for operations such as 
 * registering / unregistering of disks, open flags
 * for OpenFile and access flags */
#define __DISK_REMOVABLE						0x00000001

#define __DISK_FORCED_REMOVE					0x00000001

#define __FILE_READ_ACCESS						0x00000001
#define __FILE_WRITE_ACCESS						0x00000002
#define __FILE_READ_SHARE						0x00000100
#define __FILE_WRITE_SHARE						0x00000200

#define __FILE_CREATE							0x00000001
#define __FILE_TRUNCATE							0x00000002
#define __FILE_MUSTEXIST						0x00000004
#define __FILE_FAILONEXIST						0x00000008
#define __FILE_APPEND							0x00000100
#define __FILE_BINARY							0x00000200
#define __FILE_VOLATILE							0x00000400

/* RegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
OsStatus_t 
RegisterDisk(
	_In_ UUId_t Driver, 
	_In_ UUId_t Device, 
	_In_ Flags_t Flags);
#else
static __CRT_INLINE 
OsStatus_t 
RegisterDisk(
	_In_ UUId_t Driver, 
	_In_ UUId_t Device, 
	_In_ Flags_t Flags)
{

}
#endif

/* UnregisterDisk
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
OsStatus_t 
UnregisterDisk(
	_In_ UUId_t Device, 
	_In_ Flags_t Flags);
#else
static __CRT_INLINE 
OsStatus_t 
UnregisterDisk(
	_In_ UUId_t Device,
	_In_ Flags_t Flags)
{

}
#endif

/* OpenFile
 * Opens or creates the given file path based on
 * the given <Access> and <Options> flags. See the
 * top of this file */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
OpenFile(
	_In_ UUId_t Requester,
	_In_ __CONST char *Path, 
	_In_ Flags_t Options, 
	_In_ Flags_t Access,
	_Out_ UUId_t *Handle);
#else
static __CRT_INLINE 
FileSystemCode_t
OpenFile(
	_In_ __CONST char *Path, 
	_In_ Flags_t Options, 
	_In_ Flags_t Access,
	_Out_ UUId_t *Handle)
{

}
#endif

/* CloseFile
 * Closes the given file-handle, but does not necessarily
 * close the link to the file. Returns the result */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
CloseFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle);
#else
static __CRT_INLINE 
FileSystemCode_t 
CloseFile(
	_In_ UUId_t Handle)
{

}
#endif

/* DeleteFile
 * Deletes the given file associated with the filehandle
 * the caller must make sure there is no other references
 * to the file - otherwise delete fails */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
DeleteFile(
	_In_ UUId_t Requester, 
	_In_ __CONST char *Path);
#else
static __CRT_INLINE 
FileSystemCode_t
DeleteFile(
	_In_ __CONST char *Path)
{

}
#endif

/* ReadFile
 * Reads the requested number of bytes into the given buffer
 * from the current position in the filehandle */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
ReadFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle,
	_Out_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesRead);
#else
static __CRT_INLINE 
FileSystemCode_t
ReadFile(
	_In_ UUId_t Handle, 
	_Out_ BufferObject_t *BufferObject,
	_Out_Opt_ size_t *BytesRead)
{

}
#endif

/* WriteFile
 * Writes the requested number of bytes from the given buffer
 * into the current position in the filehandle */
#ifdef __FILEMANAGER_IMPL
__EXTERN
FileSystemCode_t
WriteFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_In_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesWritten);
#else
static __CRT_INLINE
FileSystemCode_t
WriteFile(
	_In_ UUId_t Handle,
	_In_ BufferObject_t *BufferObject,
	_Out_Opt_ size_t *BytesWritten)
{

}
#endif

/* SeekFile
 * Sets the file-pointer for the given handle to the
 * values given, the position is absolute and must
 * be within range of the file size */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
SeekFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle, 
	_In_ uint32_t SeekLo, 
	_In_ uint32_t SeekHi);
#else
static __CRT_INLINE 
FileSystemCode_t
SeekFile(
	_In_ UUId_t Handle, 
	_In_ uint32_t SeekLo, 
	_In_ uint32_t SeekHi)
{

}
#endif

/* FlushFile
 * */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
FlushFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle);
#else
static __CRT_INLINE
FileSystemCode_t
FlushFile(
	_In_ UUId_t Handle)
{

}
#endif

/* MoveFile
 * */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
FileSystemCode_t
MoveFile(
	_In_ UUId_t Requester,
	_In_ __CONST char *Source, 
	_In_ __CONST char *Destination,
	_In_ int Copy);
#else
static __CRT_INLINE 
FileSystemCode_t
MoveFile(
	_In_ __CONST char *Source,
	_In_ __CONST char *Destination,
	_In_ int Copy)
{

}
#endif

#endif //!_FILE_INTERFACE_H_
