/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
#include <crtdefs.h>
#include <stdint.h>

#include <DeviceManager.h>
#include <MollenOS.h>
#include <MString.h>
#include <Mutex.h>
#include <Events.h>

/* Additional Vfs includes */
#include <Vfs/Partition.h>

/* Definitions */
#define FILESYSTEM_FAT			0x00000000
#define FILESYSTEM_MFS			0x00000008

#define FILESYSTEM_INIT			":/System/Sapphire.mxi"

#define FILESYSTEM_IDENT_SYS	"%Sys%"

/* Error Codes for VFS Operations */
typedef enum _VfsErrorCode
{
	VfsOk,
	VfsDeleted,
	VfsInvalidParameters,
	VfsInvalidPath,
	VfsPathNotFound,
	VfsAccessDenied,
	VfsPathIsNotDirectory,
	VfsPathExists,
	VfsDiskError

} VfsErrorCode_t;

/* VFS State Codes */
typedef enum _VfsState
{
	VfsStateInit,
	VfsStateFailed,
	VfsStateActive

} VfsState_t;

/* File Flags */
typedef enum _VfsFileFlags
{
	/* Access Flags */
	Read	= 0x1,
	Write	= 0x2,

	/* Utilities */
	CreateIfNotExists = 0x4,
	TruncateIfExists = 0x8,
	FailIfExists = 0x10,
	
	/* Data Flags */
	Binary	= 0x20,
	NoBuffering = 0x40,
	Append = 0x80,

	/* Share Flags */
	ReadShare	= 0x100,
	WriteShare	= 0x200

} VfsFileFlags_t;

/* Query Functions */
typedef enum _VfsQueryFunctions
{
	/* Functions - Fs Layer */
	QueryStats = 0,
	QueryChildren,

	/* Functions - Vfs Layer */
	QueryGetAccess,
	QuerySetAccess

} VfsQueryFunction_t;

/* Vfs Special Paths */
typedef enum _VfsEnvironmentPaths
{
	/* The default */
	PathCurrentWorkingDir = 0,

	/* Application Paths */
	PathApplicationBase,
	PathApplicationData,

	/* System Directories */
	PathSystemBase,
	PathSystemDirectory,

	/* Shared Directories */
	PathCommonBin,
	PathCommonDocuments,
	PathCommonInclude,
	PathCommonLib,
	PathCommonMedia,

	/* User Directories */
	PathUserBase,

	/* Special Directory Count */
	PathEnvironmentCount

} VfsEnvironmentPath_t;

/* Definitions */
#define VFS_MAIN_DRIVE		0x1

/* Vfs Request Structure */
typedef struct _MCoreVfsRequest
{
	/* Base */
	MCoreEvent_t Base;

} MCoreVfsRequest_t;

/* Vfs Query Function
 * -- Structures */
typedef struct _VQFileStats
{
	/* Size(s) */
	uint64_t Size;
	uint64_t SizeOnDisk;

	/* RW-Position */
	uint64_t Position;

	/* Time-Info */

	/* Perms & Flags */
	int Access;
	int Flags;

} VQFileStats_t;

/* Entries are dynamic in size */
#pragma pack(push, 1)
typedef struct _VQDirEntry
{
	/* Magic */
	uint16_t Magic;

	/* Entry length 
	 * for whole structure */
	uint16_t Length;

	/* Size(s) */
	uint64_t Size;
	uint64_t SizeOnDisk;

	/* Flags */
	int Flags;

	/* Name */
	uint8_t Name[1];

} VQDirEntry_t;
#pragma pack(pop)

/* Structures */
#pragma pack(push, 1)
typedef struct _MCoreFile
{
	/* Name of Node */
	MString_t *Name;

	/* Flags */
	size_t Hash;
	int IsLocked;
	int References;

	/* Position */
	uint64_t Size;

	/* The FS structure */
	void *Fs;

	/* FS-Specific Data */
	void *Data;

} MCoreFile_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MCoreFileInstance
{
	/* Owner */
	int Id;
	//PId_t Process;

	/* Flags */
	VfsErrorCode_t Code;
	VfsFileFlags_t Flags;
	VfsFileFlags_t LastOp;
	int IsEOF;

	/* Position */
	uint64_t Position;

	/* I/O Buffer */
	void *oBuffer;
	size_t oBufferPosition;

	/* Handle */
	MCoreFile_t *File;

	/* FS-Instance Handle */
	void *Instance;

} MCoreFileInstance_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MCoreFileSystem
{
	/* Identifier */
	MString_t *Identifier;
	uint32_t Id;

	/* Flags */
	VfsState_t State;
	uint32_t Flags;

	/* Information */
	uint64_t SectorStart;
	uint64_t SectorCount;
	uint32_t SectorSize;

	/* Lock */
	Mutex_t *Lock;

	/* Disk */
	DevId_t DiskId;

	/* Filesystem-specific data */
	void *FsData;

	/* Functions */
	OsResult_t (*Destroy)(void *Fs, uint32_t Forced);

	/* Handle Operations */
	VfsErrorCode_t (*OpenFile)(void *Fs, MCoreFile_t *Handle, MString_t *Path, VfsFileFlags_t Flags);
	VfsErrorCode_t (*CloseFile)(void *Fs, MCoreFile_t *Handle);

	void (*OpenHandle)(void *Fs, MCoreFile_t *Handle, MCoreFileInstance_t *Instance);
	void (*CloseHandle)(void *Fs, MCoreFileInstance_t *Instance);
	
	/* File Operations */
	size_t (*ReadFile)(void *Fs, MCoreFile_t *Handle, MCoreFileInstance_t *Instance, uint8_t *Buffer, size_t Size);
	size_t (*WriteFile)(void *Fs, MCoreFile_t *Handle, MCoreFileInstance_t *Instance, uint8_t *Buffer, size_t Size);
	VfsErrorCode_t (*Seek)(void *Fs, MCoreFile_t *Handle, MCoreFileInstance_t *Instance, uint64_t Position);
	VfsErrorCode_t (*DeleteFile)(void *Fs, MCoreFile_t *Handle);

	/* Get's information about a node */
	VfsErrorCode_t (*Query)(void *Fs, MCoreFile_t *Handle, MCoreFileInstance_t *Instance, VfsQueryFunction_t Function, void *Buffer, size_t Length);

} MCoreFileSystem_t;
#pragma pack(pop)

/* Setup */
_CRT_EXTERN void VfsInit(void);

/* Request Operations */
_CRT_EXTERN void VfsRequestCreate(void*);
_CRT_EXTERN void VfsRequestWait(void*);

/* Register / Unregister */
_CRT_EXTERN void VfsRegisterDisk(DevId_t DiskId);
_CRT_EXTERN void VfsUnregisterDisk(DevId_t DiskId, uint32_t Forced);

/* Open & Close */
_CRT_EXTERN MCoreFileInstance_t *VfsOpen(const char *Path, VfsFileFlags_t OpenFlags);
_CRT_EXTERN VfsErrorCode_t VfsClose(MCoreFileInstance_t *Handle);
_CRT_EXTERN VfsErrorCode_t VfsDelete(MCoreFileInstance_t *Handle);

/* File Operations */
_CRT_EXTERN size_t VfsRead(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length);
_CRT_EXTERN size_t VfsWrite(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length);
_CRT_EXTERN VfsErrorCode_t VfsSeek(MCoreFileInstance_t *Handle, uint64_t Offset);
_CRT_EXTERN VfsErrorCode_t VfsFlush(MCoreFileInstance_t *Handle);

/* Directory Operations */
_CRT_EXTERN VfsErrorCode_t VfsCreatePath(const char *Path);

/* Utilities */
_CRT_EXTERN VfsErrorCode_t VfsQuery(MCoreFileInstance_t *Handle, VfsQueryFunction_t Function, void *Buffer, size_t Length);
_CRT_EXTERN VfsErrorCode_t VfsMove(const char *Path, const char *NewPath, int Copy);
_CRT_EXTERN MString_t *VfsResolveEnvironmentPath(VfsEnvironmentPath_t Base);

#endif //!_MCORE_VFS_H_