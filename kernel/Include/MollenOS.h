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
* MollenOS Common Entry Point
*/
#ifndef _MCORE_H_
#define _MCORE_H_

/* Includes */
#include <os/osdefs.h>

/* Definitions */

/* This structure is passed by mBoot 
 * in order to properly setup */
typedef struct _MCoreBootDescriptor
{
	/* Kernel Information */
	uint32_t KernelAddress;
	uint32_t KernelSize;

	/* Ramdisk Information */
	uint32_t RamDiskAddress;
	uint32_t RamDiskSize;

	/* Exports */
	uint32_t ExportsAddress;
	uint32_t ExportsSize;

	/* Symbols */
	uint32_t SymbolsAddress;
	uint32_t SymbolsSize;

} MCoreBootDescriptor;

/* This structure is needed in order to
* setup MCore */
typedef struct _MCoreBootInfo
{
	/* Bootloader Name */
	char *BootloaderName;

	/* The boot descriptor */
	MCoreBootDescriptor Descriptor;

	/* Data that will be passed to setup functions */
	void *ArchBootInfo;

	/* Setup Functions */
	void(*InitHAL)(void *ArchBootInfo, MCoreBootDescriptor *Descriptor);
	void(*InitPostSystems)(void);
	void(*InitTimers)(void);

} MCoreBootInfo_t;


#endif //!_MCORE_BOOT_INFO_H_