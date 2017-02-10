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
 * - This header describes the base filesystem-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _VFS_FILE_DEFINITIONS_H_
#define _VFS_FILE_DEFINITIONS_H_

/* Error Codes 
 * Used in standard VFS operations as return codes */
typedef enum _FileSystemCode {
	FsOk,
	FsDeleted,
	FsInvalidParameters,
	FsInvalidPath,
	FsPathNotFound,
	FsAccessDenied,
	FsPathIsNotDirectory,
	FsPathExists,
	FsDiskError
} FileSystemCode_t;

#endif //!_VFS_FILE_DEFINITIONS_H_
