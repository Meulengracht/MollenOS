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
 * MollenOS MCore - Virtual File Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _VFS_INTERFACE_H_
#define _VFS_INTERFACE_H_

/* Includes 
 * - System */
#include <os/contracts/filesystem.h>
#include <os/buffer.h>
#include <os/sharedobject.h>

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/collection.h>
#include <stddef.h>

/* VFS Definitions 
 * - General identifiers can be used in paths */
#define __FILEMANAGER_RESOLVEQUEUE		IPC_DECL_FUNCTION(10000)
#define __FILEMANAGER_MAXDISKS			64

#define __FILE_OPERATION_NONE			0x00000000
#define __FILE_OPERATION_READ			0x00000001
#define __FILE_OPERATION_WRITE			0x00000002

/* VFS FileSystem Types
 * The different supported built-in filesystems */
typedef enum _FileSystemType {
	FSUnknown				= 0,
	FSFAT,
	FSEXFAT,
	FSNTFS,
	FSHFS,
	FSHPFS,
	FSMFS,
	FSEXT
} FileSystemType_t;

/* VFS FileSystem Module 
 * Contains all the protocols implemented
 * by each filesystem module, also contains
 * the number of references the individual module */
typedef struct _FileSystemModule {
	FileSystemType_t				Type;
	int								References;
	Handle_t						Handle;

	FsInitialize_t					Initialize;
	FsDestroy_t						Destroy;
	FsOpenFile_t					OpenFile;
	FsCreateFile_t					CreateFile;
	FsCloseFile_t					CloseFile;
	FsOpenHandle_t					OpenHandle;
	FsCloseHandle_t					CloseHandle;
	FsReadFile_t					ReadFile;
	FsWriteFile_t					WriteFile;
	FsSeekFile_t					SeekFile;
	FsChangeFileSize_t				ChangeFileSize;
	FsDeletePath_t					DeletePath;
	FsQueryFile_t					QueryFile;
} FileSystemModule_t;

/* VFS FileSystem structure
 * this is the main file-system structure 
 * and contains everything related to a filesystem 
 * represented in MCore */
typedef struct _FileSystem {
	UUId_t							Id;
	FileSystemType_t				Type;
	MString_t						*Identifier;
	FileSystemDescriptor_t			Descriptor;
	FileSystemModule_t				*Module;
} FileSystem_t;

/* DiskRegisterFileSystem 
 * Registers a new filesystem of the given type, on
 * the given disk with the given position on the disk 
 * and assigns it an identifier */
__EXTERN
OsStatus_t
DiskRegisterFileSystem(
    _In_ FileSystemDisk_t*  Disk,
	_In_ uint64_t           Sector,
    _In_ uint64_t           SectorCount,
    _In_ FileSystemType_t   Type);

/* DiskDetectFileSystem
 * Detectes the kind of filesystem at the given absolute sector 
 * with the given sector count. It then loads the correct driver
 * and installs it */
__EXTERN OsStatus_t DiskDetectFileSystem(FileSystemDisk_t *Disk,
	BufferObject_t *Buffer, uint64_t Sector, uint64_t SectorCount);

/* DiskDetectLayout
 * Detects the kind of layout on the disk, be it
 * MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS */
__EXTERN OsStatus_t DiskDetectLayout(FileSystemDisk_t *Disk);

/* VfsResolveFileSystem
 * Tries to resolve the given filesystem by locating
 * the appropriate driver library for the given type */
__EXTERN FileSystemModule_t *VfsResolveFileSystem(FileSystem_t *FileSystem);

/* VfsResolveQueueExecute
 * Resolves all remaining filesystems that have been
 * waiting in the resolver-queue */
__EXTERN OsStatus_t VfsResolveQueueExecute(void);

/* VfsResolveQueueEvent
 * Sends the event to ourself that we are ready to
 * execute the resolver queue */
__EXTERN OsStatus_t VfsResolveQueueEvent(void);

/* VfsGetResolverQueue
 * Retrieves a list of all the current filesystems
 * that needs to be resolved, and is scheduled */
__EXTERN Collection_t *VfsGetResolverQueue(void);

/* VfsGetFileSystems
 * Retrieves a list of all the current filesystems
 * and provides access for manipulation */
__EXTERN Collection_t *VfsGetFileSystems(void);

/* VfsGetModules
 * Retrieves a list of all the currently loaded
 * modules, provides access for manipulation */
__EXTERN Collection_t *VfsGetModules(void);

/* VfsGetDisks
 * Retrieves a list of all the currently registered
 * disks, provides access for manipulation */
__EXTERN Collection_t *VfsGetDisks(void);

/* VfsIdentifierFileGet
 * Retrieves a new identifier for a file-handle that
 * is system-wide unique */
__EXTERN UUId_t VfsIdentifierFileGet(void);

/* VfsGetOpenFiles / VfsGetOpenHandles
 * Retrieves the list of open files /handles and allows
 * access and manipulation of the list */
__EXTERN Collection_t *VfsGetOpenFiles(void);
__EXTERN Collection_t *VfsGetOpenHandles(void);

/* VfsIdentifierAllocate 
 * Allocates a free identifier index for the
 * given disk, it varies based upon disk type */
__EXTERN
UUId_t
SERVICEABI
VfsIdentifierAllocate(
    _In_ FileSystemDisk_t *Disk);

/* VfsIdentifierFree 
 * Frees a given disk identifier index */
__EXTERN
OsStatus_t
SERVICEABI
VfsIdentifierFree(
    _In_ FileSystemDisk_t   *Disk,
    _In_ UUId_t              Id);

/* VfsRegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
__EXTERN 
OsStatus_t
SERVICEABI
VfsRegisterDisk(
	_In_ UUId_t     Driver,
	_In_ UUId_t     Device,
	_In_ Flags_t    Flags);

/* VfsUnregisterDisk
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
__EXTERN 
OsStatus_t
SERVICEABI
VfsUnregisterDisk(
	_In_ UUId_t     Device, 
	_In_ Flags_t    Flags);

/* VfsOpenFile
 * Opens or creates the given file path based on
 * the given <Access> and <Options> flags. See the
 * top of this file */
__EXTERN 
FileSystemCode_t
SERVICEABI
VfsOpenFile(
	_In_  UUId_t        Requester,
	_In_  const char*   Path, 
	_In_  Flags_t       Options, 
	_In_  Flags_t       Access,
	_Out_ UUId_t*       Handle);

/* VfsCloseFile
 * Closes the given file-handle, but does not necessarily
 * close the link to the file. Returns the result */
__EXTERN 
FileSystemCode_t
SERVICEABI
VfsCloseFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle);

/* VfsDeletePath
 * Deletes the given file path
 * the caller must make sure there is no other references
 * to the file - otherwise delete fails */
__EXTERN 
FileSystemCode_t
SERVICEABI
VfsDeletePath(
	_In_ UUId_t         Requester, 
	_In_ const char*    Path,
    _In_ Flags_t        Options);

/* VfsReadFile
 * Reads the requested number of bytes into the given buffer
 * from the current position in the file-handle */
__EXTERN
FileSystemCode_t
SERVICEABI
VfsReadFile(
	_In_  UUId_t            Requester,
	_In_  UUId_t            Handle,
	_Out_ BufferObject_t*   BufferObject,
	_Out_ size_t*           BytesIndex,
	_Out_ size_t*           BytesRead);

/* VfsWriteFile
 * Writes the requested number of bytes from the given buffer
 * into the current position in the file-handle */
__EXTERN
FileSystemCode_t
SERVICEABI
VfsWriteFile(
	_In_  UUId_t            Requester,
	_In_  UUId_t            Handle,
	_In_  BufferObject_t*   BufferObject,
	_Out_ size_t*           BytesWritten);

/* VfsSeekFile
 * Sets the file-pointer for the given handle to the
 * values given, the position is absolute and must
 * be within range of the file size */
__EXTERN 
FileSystemCode_t
SERVICEABI
VfsSeekFile(
	_In_ UUId_t     Requester,
	_In_ UUId_t     Handle, 
	_In_ uint32_t   SeekLo, 
	_In_ uint32_t   SeekHi);

/* VfsFlushFile
 * Flushes the internal file buffers and ensures there are
 * no pending file operations for the given file handle */
__EXTERN 
FileSystemCode_t
SERVICEABI
VfsFlushFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle);

/* VfsMoveFile
 * Moves or copies a given file path to the destination path
 * this can also be used for renamining if the dest/source paths
 * match (except for filename/directoryname) */
__EXTERN 
FileSystemCode_t
SERVICEABI
VfsMoveFile(
	_In_ UUId_t         Requester,
	_In_ const char*    Source, 
	_In_ const char*    Destination,
	_In_ int            Copy);

/* VfsGetFilePosition 
 * Queries the current file position that the given handle
 * is at, it returns as two separate unsigned values, the upper
 * value is optional and should only be checked for large files */
__EXTERN
OsStatus_t
SERVICEABI
VfsGetFilePosition(
	_In_  UUId_t                    Requester,
	_In_  UUId_t                    Handle,
	_Out_ QueryFileValuePackage_t*  Result);

/* VfsGetFileOptions 
 * Queries the current file options and file access flags
 * for the given file handle */
__EXTERN
OsStatus_t
SERVICEABI
VfsGetFileOptions(
	_In_  UUId_t                        Requester,
	_In_  UUId_t                        Handle,
	_Out_ QueryFileOptionsPackage_t*    Result);

/* VfsSetFileOptions 
 * Attempts to modify the current option and or access flags
 * for the given file handle as specified by <Options> and <Access> */
__EXTERN
OsStatus_t
SERVICEABI
VfsSetFileOptions(
	_In_ UUId_t     Requester,
	_In_ UUId_t     Handle,
	_In_ Flags_t    Options,
	_In_ Flags_t    Access);

/* VfsGetFileSize 
 * Queries the current file size that the given handle
 * has, it returns as two separate unsigned values, the upper
 * value is optional and should only be checked for large files */
__EXTERN
OsStatus_t
SERVICEABI
VfsGetFileSize(
	_In_  UUId_t                    Requester,
	_In_  UUId_t                    Handle,
	_Out_ QueryFileValuePackage_t*  Result);

/* VfsGetFilePath 
 * Queries the full path of a file that the given handle
 * has, it returns it as a UTF8 string with max length of _MAXPATH */
__EXTERN
OsStatus_t
SERVICEABI
VfsGetFilePath(
	_In_  UUId_t        Requester,
	_In_  UUId_t        Handle,
	_Out_ MString_t**   Path);

/* VfsPathResolveEnvironment
 * Resolves the given env-path identifier to a string
 * that can be used to locate files. */
__EXTERN 
MString_t*
SERVICEABI
VfsPathResolveEnvironment(
	_In_ EnvironmentPath_t Base);

/* VfsPathCanonicalize
 * Canonicalizes the path by removing extra characters
 * and resolving all identifiers in path */
__EXTERN 
MString_t*
SERVICEABI
VfsPathCanonicalize(
	_In_ EnvironmentPath_t  Base,
	_In_ const char*        Path);

#endif //!_VFS_INTERFACE_H_
