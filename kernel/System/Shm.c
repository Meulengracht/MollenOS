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
* MollenOS MCore - Shared Memory System
*/

/* Includes */
#include <Shm.h>

/* Globals */
MCoreShmManager_t *GlbShmManager = NULL;

/* Setup Shared Memory */
void ShmInit(void)
{
	/* Setup */
	GlbShmManager = (MCoreShmManager_t*)kmalloc(sizeof(MCoreShmManager_t));

	/* Allocate a heap */
	GlbShmManager->Heap = HeapCreate(MEMORY_LOCATION_SHM, 0);
	GlbShmManager->ProcessNodes = NULL;
}

/* Allocate Memory */
Addr_t ShmAllocateForProcess(PId_t ProcessId,
	AddressSpace_t *AddrSpace, Addr_t Address, size_t Length)
{
	/* Step 1. Allocate the memory on the heap */
	Addr_t KernelAddr = (Addr_t)umalloc(GlbShmManager->Heap, Length);
	
	/* Step 2. Get physical */
	PhysAddr_t PhysAddr = 
		AddressSpaceGetMap(AddressSpaceGetCurrent(), KernelAddr);

	/* Step 3. Map it in the given address space */
	if (!AddressSpaceGetMap(AddrSpace, Address))
		AddressSpaceMap(AddrSpace, Address, 1);

	/* Find it's node */
	if (GlbShmManager->ProcessNodes == NULL)
	{

	}
	else
	{
		/* Iterate */

	}

	return 0;
}

/* Free Memory */
Addr_t ShmFreeForProcess(PId_t ProcessId,
	AddressSpace_t *AddrSpace, Addr_t Address)
{

	return 0;
}