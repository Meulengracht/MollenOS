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

#ifndef _FILE_DEFINITIONS_H_
#define _FILE_DEFINITIONS_H_

/* Error Codes 
 * Used in standard VFS operations as return codes */
typedef enum _FileSystemCode {
	FsOk,
	FsDeleted,
	FsInvalidParameters,
	FsPathNotFound,
	FsAccessDenied,
	FsPathIsNotDirectory,
	FsPathExists,
	FsDiskError
} FileSystemCode_t;

/* Special Paths
 * Used for a combination of different things
 * especially for the environment resolve */
typedef enum _EnvironmentPath
{
	/* The default */
	PathCurrentWorkingDirectory = 0,

	/* Application Paths */
	PathApplicationBase,
	PathApplicationData,

	/* System Directories */
	PathRoot,
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

} EnvironmentPath_t;

#endif //!_FILE_DEFINITIONS_H_
