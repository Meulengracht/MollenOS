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

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <stddef.h>

/* VFS FileSystem Module 
 * Contains all the protocols implemented
 * by each filesystem module, also contains
 * the number of references the individual module */
typedef struct _FileSystemModule {
	UUId_t							Id;
	int								References;

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
	MString_t						*Identifier;
	FileSystemDescriptor_t			Descriptor;
	FileSystemModule_t				*Module;
} FileSystem_t;

/* DiskDetectFileSystem
 * Detectes the kind of filesystem at the given absolute sector 
 * with the given sector count. It then loads the correct driver
 * and installs it */
__CRT_EXTERN OsStatus_t DiskDetectFileSystem(FileSystemDisk_t *Disk,
	uint64_t StartSector, uint64_t SectorCount);

/* DiskDetectLayout
 * Detects the kind of layout on the disk, be it
 * MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS */
__CRT_EXTERN OsStatus_t DiskDetectLayout(FileSystemDisk_t *Disk);

#endif //!_VFS_INTERFACE_H_
