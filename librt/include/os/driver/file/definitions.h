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
* - Definitions & Standard Codes
*/

#ifndef _MCORE_VFS_DEFS_H_
#define _MCORE_VFS_DEFS_H_

/* Definitions
 * - Filesystem types for modules 
 *   used to load the correct module */
#define FILESYSTEM_FAT			0x00000000
#define FILESYSTEM_MFS			0x00000008

/* Definitions 
 * - The INIT program to run for user-space
 *   usually the window manager */
#define FILESYSTEM_INIT			":/System/Sapphire.mxi"

/* Definitions 
 * - General identifiers can be used in paths */
#define FILESYSTEM_IDENT_SYS	"%Sys%"

/* Definitions 
 * Used when installing the FS as a flag
 * for which drive was used to boot the OS */
#define VFS_MAIN_DRIVE		0x1

/* Error Codes 
 * Used in standard VFS operations as return codes */
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

/* State Codes for FS 
 * Used to describe the currrent FS state */
typedef enum _VfsState
{
	VfsStateInit,
	VfsStateFailed,
	VfsStateActive

} VfsState_t;

/* Special Paths
 * Used for a combination of different things
 * especially for the environment resolve */
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


#endif //!_MCORE_VFS_DEFS_H_
