/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - Virtual FileSystem
*/
#ifndef _MCORE_VFS_H_
#define _MCORE_VFS_H_

/* Includes */
#include <Devices/Disk.h>
#include <Mutex.h>
#include <crtdefs.h>
#include <stdint.h>

/* Additional Vfs includes */
#include <Vfs/Partition.h>

/* Error Codes for VFS Operations */
typedef enum _VfsErrorCode
{
	VfsOk,
	VfsInvalidParameters,
	VfsPathNotFound,
	VfsAccessDenied
} VfsErrorCode_t;

/* File Flags */
typedef enum _VfsFileFlags
{
	/* Access Flags */
	Read	= 0x1,
	Write	= 0x2,
	
	/* Data Flags */
	Binary	= 0x4,
	NoBuffering = 0x8,

	/* Share Flags */
	ReadShare	= 0x10,
	WriteShare	= 0x20

} VfsFileFlags_t;

/* Definitions */
#define VFS_MAIN_DRIVE		0x1

/* Structures */
#pragma pack(push, 1)
typedef struct _MCoreFile
{
	/* Name of Node */
	char *Name;

	/* Flags */
	VfsFileFlags_t Flags;
	uint32_t IsEOF;

	/* Position & size */
	uint64_t Position;
	uint64_t Size;

	/* I/O Buffer */
	void *iBuffer;
	void *oBuffer;

	/* FS-Specific Data */
	void *Data;

} MCoreFile_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MCoreFileSystem
{
	/* Identifier */
	char *Identifier;

	/* Flags */
	uint32_t Flags;

	/* Information */
	uint64_t SectorStart;
	uint64_t SectorCount;

	/* Lock */
	Mutex_t *Lock;

	/* Disk */
	MCoreStorageDevice_t *Disk;

	/* Filesystem-specific data */
	void *FsData;

	/* Functions */
	OsResult_t (*Destory)(void *FsData, uint32_t Forced);

	/* Handle Operations */
	VfsErrorCode_t (*OpenFile)(void *FsData, MCoreFile_t *Handle, char *Path, VfsFileFlags_t Flags);
	VfsErrorCode_t (*CloseFile)(void *FsData, MCoreFile_t *Handle);
	
	/* File Operations */
	VfsErrorCode_t (*ReadFile)(void *FsData, MCoreFile_t *Handle, void *Buffer, uint32_t Size);
	VfsErrorCode_t (*WriteFile)(void *FsData, MCoreFile_t *Handle, void *Buffer, uint32_t Size);
	VfsErrorCode_t (*Seek)(void *FsData, MCoreFile_t *Handle, uint64_t Offset);
	VfsErrorCode_t (*Flush)(void *FsData, MCoreFile_t *Handle);

	/* Utility */
	VfsErrorCode_t (*Delete)(void *FsData, MCoreFile_t *Handle);
	VfsErrorCode_t (*Rename)(void *FsData, MCoreFile_t *Handle);

	/* Get's information about a node */
	VfsErrorCode_t (*Query)(void *FsData, MCoreFile_t *Handle);

} MCoreFileSystem_t;
#pragma pack(pop)

/* Setup */
_CRT_EXTERN void VfsInit(void);

/* Register / Unregister */
_CRT_EXTERN void VfsRegisterDisk(MCoreStorageDevice_t *Disk);
_CRT_EXTERN void VfsUnregisterDisk(MCoreStorageDevice_t *Disk, uint32_t Forced);

#endif //!_MCORE_VFS_H_