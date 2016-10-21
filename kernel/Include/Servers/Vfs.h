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
* MollenOS MCore - Virtual FileSystem Server
*/

#ifndef _MCORE_SERVER_VFS_H_
#define _MCORE_SERVER_VFS_H_

/* Includes 
 * - C-Library */
#include <os/osdefs.h>
#include <os/ipc.h>
#include <ds/mstring.h>
#include <stddef.h>

/* Includes
 * - VFS */
#include <Vfs/Definitions.h>
#include <Vfs/Partition.h>
#include <Vfs/File.h>

/* VFS Request Types 
 * These are the possible requests
 * to make for the VFS */
typedef enum _MCoreVfsRequestType
{
	/* Disk Operations */
	VfsRequestRegisterDisk,
	VfsRequestUnregisterDisk,

	/* VFS Operations */
	VfsRequestOpenFile,
	VfsRequestCloseFile,
	VfsRequestDeleteFile,
	VfsRequestReadFile,
	VfsRequestWriteFile,
	VfsRequestSeekFile,
	VfsRequestFlushFile

} MCoreVfsRequestType_t;

/* VFS IPC Event System
 * This is the request structure for 
 * making any VFS related requests */
typedef struct _MCoreVfsRequest
{
	/* IPC Base */
	MEventMessageBase_t Base;

	/* Type of request */
	MCoreVfsRequestType_t Type;

	/* Pointer data (params) 
	 * Which one is used depends on the request */
	union {
		MCoreFileInstance_t *Handle;
		const char *Path;
	} Pointer;

	/* Buffer data (params)
	 * This is used by read, write and query for buffers */
	uint8_t *Buffer;

	/* Value data (params)
	 * Which and how this is used is based on request 
	 * This supports up to 64 bit */
	union {
		union {
			uint32_t Length;
			DevId_t DiskId;
			VfsQueryFunction_t Function;
			VfsFileFlags_t Flags;
			int Copy;
		} Lo;
		union {
			uint32_t Length;
			int Forced;
		} Hi;
	} Value;
	
	/* Error code if anything 
	 * happened during ops */
	VfsErrorCode_t Error;

} MCoreVfsRequest_t;

#endif //!_MCORE_SERVER_VFS_H_