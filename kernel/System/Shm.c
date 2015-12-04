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
	/* Sanity, if page is already mapped 
	 * we need to lookup that the requested range is free */
	if (AddressSpaceGetMap(AddrSpace, Address))
	{

	}

	/* Step 1. Allocate the memory on the heap */
	Addr_t KernelAddr = (Addr_t)umalloc(GlbShmManager->Heap, Length);
	
	/* Step 2. Get physical */
	PhysAddr_t PhysAddr = 
		AddressSpaceGetMap(AddressSpaceGetCurrent(), KernelAddr);

	/* Step 3. Map it in the given address space */
	if (!AddressSpaceGetMap(AddrSpace, Address))
		AddressSpaceMapFixed(AddrSpace, PhysAddr, Address, 1);

	/* Allocate the block */
	MCoreShmBlock_t *pBlock = (MCoreShmBlock_t*)kmalloc(sizeof(MCoreShmBlock_t));

	/* Setup */
	pBlock->Allocated = 1;
	pBlock->KernelAddress = KernelAddr;
	pBlock->ProcAddress = Address;
	pBlock->Link = NULL;
	pBlock->Length = Length;

	/* Iterate */
	MCoreShmNode_t *CurrNode = GlbShmManager->ProcessNodes, *PrevNode = NULL;
	while (CurrNode)
	{
		/* Match? */
		if (CurrNode->ProcessId == ProcessId)
		{
			/* Yay */
			MCoreShmBlock_t *CurrBlock = CurrNode->Blocks, *PrevBlock = NULL;
			while (CurrBlock)
			{
				/* Next */
				PrevBlock = CurrBlock;
				CurrBlock = CurrBlock->Link;
			}

			/* Sanity */
			if (PrevBlock == NULL)
				CurrNode->Blocks = pBlock;
			else
				PrevBlock->Link = pBlock;

			/* Done */
			break;
		}

		/* Next */
		PrevNode = CurrNode;
		CurrNode = CurrNode->Link;
	}

	/* Sanity */
	if (CurrNode == NULL)
	{
		/* Didn't exist */
		MCoreShmNode_t *pNode = (MCoreShmNode_t*)kmalloc(sizeof(MCoreShmNode_t));

		/* Set */
		pNode->Link = NULL;
		pNode->Blocks = pBlock;
		pNode->ProcessId = ProcessId;

		/* Update initial pointer */
		if (PrevNode == NULL)
			GlbShmManager->ProcessNodes = pNode;
		else
			PrevNode->Link = pNode;
	}

	/* Done */
	return KernelAddr;
}

/* Free Memory */
void ShmFreeForProcess(PId_t ProcessId,
	AddressSpace_t *AddrSpace, Addr_t Address)
{
	_CRT_UNUSED(ProcessId);
	_CRT_UNUSED(AddrSpace);
	_CRT_UNUSED(Address);
}