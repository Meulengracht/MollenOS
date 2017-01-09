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
* - File Definitions & Structures
*/

#ifndef _MCORE_VFS_FILE_H_
#define _MCORE_VFS_FILE_H_

/* Includes
* - C-Library */
#include <os/osdefs.h>

/* Includes
* - VFS */
#include <Vfs/Definitions.h>

/* File flags, these are the different
* types of options an open-file operation
* can possess */
typedef enum _VfsFileFlags
{
	/* Access Flags */
	Read = 0x1,
	Write = 0x2,

	/* Utilities */
	CreateIfNotExists = 0x4,
	TruncateIfExists = 0x8,
	FailIfExists = 0x10,

	/* Data Flags */
	Binary = 0x20,
	NoBuffering = 0x40,
	Append = 0x80,

	/* Share Flags */
	ReadShare = 0x100,
	WriteShare = 0x200

} VfsFileFlags_t;

/* Query Functions, these are the different
 * queries that can be made on file-handles 
 * and varies from stats to children to security */
typedef enum _VfsQueryFunctions
{
	/* Functions - Fs Layer */
	QueryStats = 0,
	QueryChildren,

	/* Functions - Vfs Layer */
	QueryGetAccess,
	QuerySetAccess

} VfsQueryFunction_t;

/* The shared file structure
 * this is only opened once per file 
 * which is why we cache its path */
#pragma pack(push, 1)
typedef struct _MCoreFile
{
	/* Full Path
	 * & Name of Node */
	MString_t *Path;
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

/* This is the per-handle file instance
 * structure, so multiple handles can be opened
 * on just a single file, it refers to a file structure */
#pragma pack(push, 1)
typedef struct _MCoreFileInstance
{
	/* The file-handle id */
	int Id;

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

/* Vfs Query Function
 * - Query for file information */
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

/* Vfs Query Function
 * - Query for children, this is only
 *   valid when the file-handle is of type
 *   structure. Entries are dynamic in size.
 *   Last entry is always a NULL entry */
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

#endif //!_MCORE_VFS_FILE_H_
