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
* MollenOS USB EHCI Controller Driver
*/

/* Includes */
#include <Module.h>
#include "Ehci.h"

/* Additional Includes */
#include <DeviceManager.h>
#include <UsbCore.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>
#include <Pci.h>

/* CLib */
#include <string.h>

/* Initialize the Periodic Scheduler */
void EhciInitQueues(EhciController_t *Controller)
{
	/* Vars */
	Addr_t pSpace = 0, Phys = 0;
	size_t i;

	/* The first thing we want to do is 
	 * to determine the size of the frame list */
	if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST)
		Controller->FLength = 256; /* Default to shortest list */
	else
		Controller->FLength = 1024;

	/* Allocate the frame list */
	Controller->FrameList = (uint32_t*)AddressSpaceMap(AddressSpaceGetCurrent(), 
		0, (Controller->FLength * sizeof(uint32_t)), ADDRESS_SPACE_FLAG_LOWMEM);

	/* Instantiate them all to nothing */
	for (i = 0; i < Controller->FLength; i++)
		Controller->FrameList[i] = EHCI_LINK_END;

	/* Allocate the Qh Pool */
	pSpace = (Addr_t)kmalloc((sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH) + EHCI_STRUCT_ALIGN);

	/* Align with roundup */
	pSpace = ALIGN(pSpace, EHCI_STRUCT_ALIGN, 1);

	/* Zero it out */
	memset((void*)pSpace, 0, (sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH));

	/* Get physical */
	Phys = AddressSpaceGetMap(AddressSpaceGetCurrent(), pSpace);

	/* Initialise ED Pool */
	for (i = 0; i < EHCI_POOL_NUM_QH; i++)
	{
		/* Set */
		Controller->QhPool[i] = (EhciQueueHead_t*)pSpace;
		Controller->QhPool[i]->PhysicalAddress = Phys;

		/* Increament */
		pSpace += sizeof(EhciQueueHead_t);
		Phys += sizeof(EhciQueueHead_t);
	}

	/* Initialize Dummy */
	Controller->QhPool[EHCI_POOL_QH_NULL]->NextTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->NextAlternativeTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->LinkPointer = EHCI_LINK_END;

	/* Allocate the Async Dummy */
	pSpace = (Addr_t)kmalloc(sizeof(EhciTransferDescriptor_t) + EHCI_STRUCT_ALIGN);
	pSpace = ALIGN(pSpace, EHCI_STRUCT_ALIGN, 1);
	Controller->TdAsync = (EhciTransferDescriptor_t*)pSpace;

	/* Initialize the Async Dummy */
	Controller->TdAsync->Status = EHCI_TD_HALTED;
	Controller->TdAsync->Link = EHCI_LINK_END;
	Controller->TdAsync->AlternativeLink = EHCI_LINK_END;

	/* Initialize Async */
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Flags = EHCI_QH_RECLAMATIONHEAD;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Status = EHCI_TD_HALTED;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->NextTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->NextAlternativeTD =
		AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Controller->TdAsync);

	/* Allocate Transaction List */
	Controller->TransactionList = list_create(LIST_SAFE);

	/* Write addresses */
	Controller->OpRegisters->PeriodicListAddr = (uint32_t)Controller->FrameList;
	Controller->OpRegisters->AsyncListAddress = (uint32_t)Controller->QhPool[EHCI_POOL_QH_ASYNC]->PhysicalAddress;
}