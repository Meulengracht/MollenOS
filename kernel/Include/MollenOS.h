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
* MollenOS Common Entry Point
*/
#ifndef _MCORE_H_
#define _MCORE_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Typedefs */
typedef enum _OsResult
{
	OsOk,
	OsFail
} OsResult_t;

/* Definitions */

/* This structure is needed in order to
* setup MCore */
typedef struct _MCoreBootInfo
{
	/* Bootloader Name */
	char *BootloaderName;

	/* Size of kernel in bytes */
	uint32_t KernelSize;

	/* Data that will be passed to setup functions */
	void *ArchBootInfo;

	/* Setup Functions */
	void(*InitHAL)(void *ArchBootInfo);
	void(*InitPostSystems)(void);

} MCoreBootInfo_t;


#endif //!_MCORE_BOOT_INFO_H_