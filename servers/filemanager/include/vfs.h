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
#include <os/driver/contracts/filesystem.h>
#include <os/driver/buffer.h>
#include <os/sharedobject.h>

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/list.h>
#include <stddef.h>

/* VFS Definitions */
#define FILESYSTEM_INIT			":/System/Sapphire.mxi"

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
	FsCloseFile_t					CloseFile;
	FsOpenHandle_t					OpenHandle;
	FsCloseHandle_t					CloseHandle;
	FsReadFile_t					ReadFile;
	FsWriteFile_t					WriteFile;
	FsSeekFile_t					SeekFile;
	FsDeleteFile_t					DeleteFile;
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
__EXTERN OsStatus_t DiskRegisterFileSystem(FileSystemDisk_t *Disk,
	uint64_t Sector, uint64_t SectorCount, FileSystemType_t Type);

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

/* VfsGetFileSystems
 * Retrieves a list of all the current filesystems
 * and provides access for manipulation */
__EXTERN List_t *VfsGetFileSystems(void);

/* VfsGetModules
 * Retrieves a list of all the currently loaded
 * modules, provides access for manipulation */
__EXTERN List_t *VfsGetModules(void);

/* VfsIdentifierAllocate 
 * Allocates a free identifier index for the
 * given disk, it varies based upon disk type */
__EXTERN UUId_t VfsIdentifierAllocate(FileSystemDisk_t *Disk);

/* VfsIdentifierFree 
 * Frees a given identifier index */
__EXTERN OsStatus_t VfsIdentifierFree(UUId_t Id);

#endif //!_VFS_INTERFACE_H_
