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
* MollenOS MCore - MollenOS Module Manager
*/

/* Includes */
#include <Modules/ModuleManager.h>
#include <Modules/RamDisk.h>
#include <Modules/PeLoader.h>
#include <List.h>
#include <Heap.h>
#include <stdio.h>

/* Globals */
uint32_t GlbModMgrInitialized = 0;
list_t *GlbModMgrModules = NULL;

/* Loads the RD */
void ModuleMgrInit(Addr_t RamDiskAddr, uint32_t RamDiskSize)
{
	/* Parse RamDisk */
	MCoreRamDiskHeader_t *RdHeader = (MCoreRamDiskHeader_t*)RamDiskAddr;

	/* Sanity */
	if (RamDiskAddr == 0
		|| RamDiskSize == 0)
		return;

	/* Validate Members */
	if (RdHeader->Magic != RAMDISK_MAGIC)
	{
		/* Error! */
		printf("ModuleManager: Invalid Magic in Ramdisk - 0x%x\n", RdHeader->Magic);
		return;
	}

	/* Valid Version? */
	if (RdHeader->Version != RAMDISK_VERSION_1)
	{
		/* Error! */
		printf("ModuleManager: Invalid RamDisk Version - 0x%x\n", RdHeader->Version);
		return;
	}

	/* Allocate list */
	GlbModMgrModules = list_create(LIST_NORMAL);

	/* Save Module-Count */
	uint32_t FileCount = RdHeader->FileCount;
	Addr_t RdPtr = RamDiskAddr + sizeof(MCoreRamDiskHeader_t);

	/* Point to first entry */
	MCoreRamDiskFileHeader_t *FilePtr = (MCoreRamDiskFileHeader_t*)RdPtr;

	/* Iterate */
	while (FileCount != 0)
	{
		/* We only care about modules */
		if (FilePtr->Type == RAMDISK_MODULE)
		{
			/* Get a pointer to the module header */
			MCoreModule_t *Module = (MCoreModule_t*)(RamDiskAddr + FilePtr->DataOffset);

			/* Add to list */
			list_append(GlbModMgrModules, list_create_node(0, Module));
		}

		/* Next! */
		FilePtr++;
		FileCount--;
	}

	/* Done! */
	GlbModMgrInitialized = 1;
}