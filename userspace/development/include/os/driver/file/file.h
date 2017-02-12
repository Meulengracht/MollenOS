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

#ifndef _VFS_FILE_INTERFACE_H_
#define _VFS_FILE_INTERFACE_H_

/* Includes
 * - C-Library */
#include <ds/mstring.h>
#include <os/osdefs.h>

/* The shared file structure
 * Used as a file-definition by the filemanager
 * and the loaded filesystem modules */
typedef struct _FileSystemFile {
	MString_t				*Path;
	MString_t				*Name;
	size_t					Hash;
	int						IsLocked;
	int						References;
	uint64_t				Size;
	uintptr_t				*ExtensionData;
} FileSystemFile_t;

/* This is the per-handle file instance
 * structure, so multiple handles can be opened
 * on just a single file, it refers to a file structure */
typedef struct _FileSystemFileHandle {
	UUId_t					Id;
	Flags_t					Flags;
	Flags_t					LastOperation;
	uint64_t				Position;
	void					*oBuffer;
	size_t					oBufferPosition;
	FileSystemFile_t		*File;
	uintptr_t				*ExtensionData;
} FileSystemFileHandle_t;

#endif //!_VFS_FILE_INTERFACE_H_
