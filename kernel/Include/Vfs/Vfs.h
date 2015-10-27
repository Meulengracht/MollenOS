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
#include <crtdefs.h>
#include <stdint.h>

/* Additional Vfs includes */
#include <Vfs/Partition.h>

/* Definitions */


/* Structures */
typedef struct _MCoreFileSystem
{
	/* Identifier */

	/* Flags */

	/* Disk */
	MCoreStorageDevice_t *Disk;

	/* Functions */


} MCoreFileSystem_t;

/* Setup */
_CRT_EXTERN void VfsInit(void);

/* Register / Unregister */
_CRT_EXTERN void VfsRegisterDisk(MCoreStorageDevice_t *Disk);
_CRT_EXTERN void VfsUnregisterDisk(MCoreStorageDevice_t *Disk);

#endif //!_MCORE_VFS_H_