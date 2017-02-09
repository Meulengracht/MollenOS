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
 * - C-Library */
#include <os/osdefs.h>
#include <ds/mstring.h>
#include <stddef.h>

/* Includes
 * - VFS */
#include "definitions.h"
#include "partition.h"
#include "file.h"

/* VFS FileSystem structure
 * this is the main file-system structure 
 * and contains everything related to a filesystem 
 * represented in MCore */
#pragma pack(push, 1)
typedef struct _MCoreFileSystem
{
	/* FileSystem Id */
	int Id;

	/* The disk it longs to */
	UUId_t DiskId;

	/* Identifier of this filesystem
	 * used for ex as St0, St1, etc 
	 * or Rm0 if removable */
	MString_t *Identifier;

	/* Information relating to the current
	 * state and current flags on this FS */
	VfsState_t State;
	Flags_t Flags;

	/* Sector information, where it starts on disk
	 * the count of sectors and sector size */
	uint64_t SectorStart;
	uint64_t SectorCount;
	size_t SectorSize;

	/* Filesystem-specific data 
	 * this is private to the underlying FS */
	void *ExtendedData;

	/* Destroy FS, this flushes and cleans up
	 * the underlying FS, and indicates whether it was a 
	 * forced operation (no flush) or user-specified */
	OsStatus_t (*Destroy)(void *Fs, int Forced);

	/* Open file, it takes a few params 
	 * It takes the FS, a file handle it can fill in
	 * the filepath and open flags */
	VfsErrorCode_t (*OpenFile)(void *Fs, MCoreFile_t *Handle, MString_t *Path, VfsFileFlags_t Flags);

	/* Close file, cleans up the file, and resources
	 * allocated for that file handle */
	VfsErrorCode_t (*CloseFile)(void *Fs, MCoreFile_t *Handle);

	/* Open handle, this creates a new handle for an
	 * open file, and resets it for the requester */
	VfsErrorCode_t (*OpenHandle)(void *Fs, MCoreFile_t *Handle, MCoreFileInstance_t *Instance);

	/* Close handle, this does not neccessarily close 
	 * the file it belongs to, only if no more references
	 * are on that file */
	VfsErrorCode_t (*CloseHandle)(void *Fs, MCoreFileInstance_t *Instance);
	
	/* File Operations 
	 * These include Read, Write, Seek and Delete */
	size_t (*ReadFile)(void *Fs, MCoreFileInstance_t *Instance, uint8_t *Buffer, size_t Size);
	size_t (*WriteFile)(void *Fs, MCoreFileInstance_t *Instance, uint8_t *Buffer, size_t Size);
	VfsErrorCode_t (*SeekFile)(void *Fs, MCoreFileInstance_t *Instance, uint64_t Position);
	VfsErrorCode_t (*DeleteFile)(void *Fs, MCoreFile_t *Handle);

	/* The Query function, queries information
	 * about a given file-handle */
	VfsErrorCode_t (*Query)(void *Fs, MCoreFileInstance_t *Instance, 
		VfsQueryFunction_t Function, void *Buffer, size_t Length);

} MCoreFileSystem_t;
#pragma pack(pop)

/* VfsInit
 * Initializes the virtual filesystem and 
 * all resources related, and starts the VFSEventLoop */
__CRT_EXTERN void VfsInit(void);

/* VfsRequestCreate
 * - Create a new request for the VFS */
__CRT_EXTERN void VfsRequestCreate(MCoreVfsRequest_t *Request);

/* VfsRequestWait 
 * - Wait for a request to complete, thread 
 *   will sleep/block for the duration */
__CRT_EXTERN void VfsRequestWait(MCoreVfsRequest_t *Request, size_t Timeout);

/* The Query function, queries information
 * about a given file-handle */
__CRT_EXTERN VfsErrorCode_t VfsQuery(MCoreFileInstance_t *Handle,
	VfsQueryFunction_t Function, void *Buffer, size_t Length);

/* Vfs - Resolve Environmental Path
 * @Base - Environmental Path */
__CRT_EXTERN MString_t *VfsResolveEnvironmentPath(VfsEnvironmentPath_t Base);

#endif //!_VFS_INTERFACE_H_