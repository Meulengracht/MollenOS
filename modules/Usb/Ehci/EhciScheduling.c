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
void EhciInitializePeriodicScheduler(EhciController_t *Controller)
{
	/* Vars */
	int i;

	/* The first thing we want to do is 
	 * to determine the size of the frame list */
	if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST)
		Controller->FLength = 256; /* Default to shortest list */
	else
		Controller->FLength = 1024;

	/* Allocate the frame list */
	Controller->FrameList = AddressSpaceMap(AddressSpaceGetCurrent(), 
		0, (Controller->FLength * sizeof(uint32_t)), ADDRESS_SPACE_FLAG_LOWMEM);

	/* Instantiate them all to nothing */
	for (i = 0; i < Controller->FLength; i++)
		Controller->FrameList[i] = EHCI_LINK_END;

	/* Write address */
	Controller->OpRegisters->PeriodicListAddr = (uint32_t)Controller->FrameList;
}

/* Initialize the Async Scheduler */
void EhciInitializeAsyncScheduler(EhciController_t *Controller)
{
	/* Allocate the Qh Pool */
	Addr_t pSpace = (Addr_t)kmalloc((sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH) + EHCI_STRUCT_ALIGN);
	int i;
	
	/* Align with roundup */
	ALIGNVAL(pSpace, EHCI_STRUCT_ALIGN, 1);

	/* Zero it out */
	memset((void*)pSpace, 0, (sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH));

	/* Initialise ED Pool */
	for (i = 0; i < EHCI_POOL_NUM_QH; i++)
	{
		/* Allocate */
		Controller->QhPool[i] = (EhciQueueHead_t*)pSpace;

		/* Increament */
		pSpace += sizeof(EhciQueueHead_t);
	}

	/* Initialize Dummy */
	Controller->QhPool[EHCI_POOL_QH_NULL]->NextTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->NextAlternativeTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->LinkPointer = EHCI_LINK_END;

	/* Initialize Async */
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Flags = EHCI_QH_RECLAMATIONHEAD;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Status = EHCI_TD_HALTED;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->NextTD = EHCI_LINK_END;


	/* Allocate Transaction List */
	Controller->TransactionList = list_create(LIST_SAFE);
}