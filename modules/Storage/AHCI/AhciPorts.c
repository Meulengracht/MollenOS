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
* MollenOS MCore - Advanced Host Controller Interface Driver
*/

/* Includes */
#include "Ahci.h"

/* Additional Includes */
#include <DeviceManager.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>

/* CLib */
#include <stddef.h>
#include <string.h>

/* AHCIPortCreate
 * Initializes the port structure, but not memory structures yet */
AhciPort_t *AhciPortCreate(AhciController_t *Controller, int Port, int Index)
{
	/* Variables */
	AhciPort_t *AhciPort = NULL;

	/* Sanity checks */
	if (Controller->Ports[Port] != NULL
		|| Port >= AHCI_MAX_PORTS) {
		return NULL;
	}

	/* Allocate some memory for our new port */
	AhciPort = (AhciPort_t*)kmalloc(sizeof(AhciPort_t));
	memset(AhciPort, 0, sizeof(AhciPort_t));

	/* Set id */
	AhciPort->Id = Port;
	AhciPort->Index = Index;

	/* Get a pointer to port registers */
	AhciPort->Registers = (volatile AHCIPortRegisters_t*)
		((uint8_t*)Controller->Registers + AHCI_REGISTER_PORTBASE(Port));

	/* Reset lock */
	SpinlockReset(&AhciPort->Lock);

	/* Create list */
	AhciPort->Transactions = ListCreate(KeyInteger, LIST_SAFE);

	/* Done, return it! */
	return AhciPort;
}

/* AHCIPortInit 
 * Initializes the memory regions and enables them in the port */
void AhciPortInit(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Variables */
	uint8_t *CmdTablePtr = NULL;
	Addr_t PhysAddress = 0;
	int i;

	/* Initialize memory structures 
	 * Both RecievedFIS and PRDT */
	Port->CommandList = (AHCICommandList_t*)((uint8_t*)Controller->CmdListBase + (1024 * Port->Id));
	Port->CommandTable = (void*)((uint8_t*)Controller->CmdTableBase 
		+ ((AHCI_COMMAND_TABLE_SIZE  * 32) * Port->Id));

	/* Get FIS base */
	if (Controller->Registers->Capabilities & AHCI_CAPABILITIES_FBSS) {
		Port->RecievedFis = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (0x1000 * Port->Id));
	}
	else {
		Port->RecievedFis = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (256 * Port->Id));
	}

	/* Setup mem pointer */
	CmdTablePtr = (uint8_t*)Port->CommandTable;

	/* Iterate command headers */
	for (i = 0; i < 32; i++) {
		
		/* Get physical address of pointer */
		Addr_t CmdTablePhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)CmdTablePtr);

		/* Setup flags */
		Port->CommandList->Headers[i].Flags = 0;
		Port->CommandList->Headers[i].TableLength = AHCI_PORT_PRDT_COUNT;
		Port->CommandList->Headers[i].PRDByteCount = 0;

		/* Set command table address */
		Port->CommandList->Headers[i].CmdTableBaseAddress = LODWORD(CmdTablePhys);

		/* Set command table address upper register 
		 * Only set upper if we are in 64 bit */
		Port->CommandList->Headers[i].CmdTableBaseAddressUpper = 
			(sizeof(void*) > 4) ? HIDWORD(CmdTablePhys) : 0;

		/* Increament pointer */
		CmdTablePtr += AHCI_COMMAND_TABLE_SIZE;
	}

	/* Update registers */
	PhysAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Port->CommandList);
	Port->Registers->CmdListBaseAddress = LODWORD(PhysAddress);
	Port->Registers->CmdListBaseAddressUpper = (sizeof(void*) > 4) ? HIDWORD(PhysAddress) : 0;

	PhysAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Port->RecievedFis);
	Port->Registers->FISBaseAddress = LOWORD(PhysAddress);
	Port->Registers->FISBaseAdressUpper = (sizeof(void*) > 4) ? HIDWORD(PhysAddress) : 0;

	/* Flush */
	MemoryBarrier();

	/* After setting PxFB and PxFBU to the physical address of the FIS receive area,
	 * system software shall set PxCMD.FRE to ‘1’. */
	Port->Registers->CommandAndStatus |= AHCI_PORT_FRE;

	/* Flush */
	MemoryBarrier();

	/* For each implemented port, clear the PxSERR register, 
	 * by writing ‘1s’ to each implemented bit location. */
	Port->Registers->AtaError = AHCI_PORT_SERR_CLEARALL;
	Port->Registers->InterruptStatus = 0xFFFFFFFF;
	
	/* Determine which events should cause an interrupt, 
	 * and set each implemented port’s PxIE register with the appropriate enables. */
	Port->Registers->InterruptEnable = (uint32_t)AHCI_PORT_IE_CPDE | AHCI_PORT_IE_TFEE
		| AHCI_PORT_IE_PCE | AHCI_PORT_IE_DSE | AHCI_PORT_IE_PSE | AHCI_PORT_IE_DHRE;
}

/* AHCIPortCleanup
 * Destroys a port, cleans up device, cleans up memory and resources */
void AhciPortCleanup(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Variables */
	ListNode_t *pNode;

	/* Set it null in the controller */
	Controller->Ports[Port->Index] = NULL;

	/* Cleanup list */
	_foreach(pNode, Port->Transactions)
	{
		/* Cast */
		void *PayLoad = pNode->Data;

		/* Cleanup */
		kfree(PayLoad);
	}

	/* Destroy the list */
	ListDestroy(Port->Transactions);

	/* Free the port structure */
	kfree(Port);
}

/* AHCIPortReset
 * Resets the port, and resets communication with the device on the port
 * if the communication was destroyed */
OsStatus_t AhciPortReset(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Unused */
	_CRT_UNUSED(Controller);

	/* Software causes a port reset (COMRESET) by writing 1h to the PxSCTL.DET */
	reg32_t Temp = Port->Registers->AtaControl;

	/* Remove current status, set reset */
	Temp &= ~(AHCI_PORT_SCTL_DET_MASK);
	Temp |= AHCI_PORT_SCTL_DET_RESET;
	
	/* Do the reset */
	Port->Registers->AtaControl |= Temp;
	MemoryBarrier();

	/* wait at least 1 millisecond before clearing PxSCTL.DET to 0h */
	SleepMs(2);

	/* After clearing PxSCTL.DET to 0h, software should wait for 
	 * communication to be re-established as indicated by PxSSTS.DET 
	 * being set to 3h. */
	Port->Registers->AtaControl &= ~(AHCI_PORT_SCTL_DET_MASK);
	MemoryBarrier();
	WaitForCondition((Port->Registers->AtaStatus & AHCI_PORT_SSTS_DET_ENABLED), 10, 25,
		"Port status never reached communication established, proceeding anyway.");

	/* Then software should write all 1s to the PxSERR register to clear 
	 * any bits that were set as part of the port reset. */
	Port->Registers->AtaError = AHCI_PORT_SERR_CLEARALL;

	/* When PxSCTL.DET is set to 1h, the HBA shall reset PxTFD.STS to 7Fh and 
	 * shall reset PxSSTS.DET to 0h. When PxSCTL.DET is set to 0h, upon receiving a 
	 * COMINIT from the attached device, PxTFD.STS.BSY shall be set to ’1’ by the HBA. */
	return OsNoError;
}

/* AHCIPortSetupDevice
 * Identifies connection on a port, and initializes connection/device */
void AhciPortSetupDevice(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Start command engine */
	Port->Registers->CommandAndStatus |= AHCI_PORT_ST | AHCI_PORT_FRE;

	/* Detect present ports using
	 * PxTFD.STS.BSY = 0, PxTFD.STS.DRQ = 0, and PxSSTS.DET = 3 */
	if (Port->Registers->TaskFileData & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)
		|| (AHCI_PORT_STSS_DET(Port->Registers->AtaStatus) 
			!= AHCI_PORT_SSTS_DET_ENABLED)) {
		return;
	}

	/* Update port status */
	LogInformation("AHCI", "Device present 0x%x on port %i", 
		Port->Registers->Signature, Port->Id);
	Port->Connected = 1;

	/* Identify device */
	AhciDeviceIdentify(Controller, Port);
}

/* AHCIPortAcquireCommandSlot
 * Allocates an available command slot on a port
 * returns index on success, otherwise -1 */
int AhciPortAcquireCommandSlot(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Variables */
	uint32_t AtaActive = Port->Registers->AtaActive;
	int i;

	/* Lock access to port, we don't 
	 * want simoultanous access */
	SpinlockAcquire(&Port->Lock);

	/* Iterate */
	for (i = 0; i < (int)Controller->CmdSlotCount; i++)
	{
		/* Check availability status 
		 * on this command slot */
		if ((Port->SlotStatus & (1 << i)) != 0
			|| (AtaActive & (1 << i)) != 0)
			continue;

		/* Allocate slot */
		Port->SlotStatus |= (1 << i);
		
		/* Release lock */
		SpinlockRelease(&Port->Lock);

		/* Return index */
		return i;
	}

	/* Release lock */
	SpinlockRelease(&Port->Lock);

	/* Damn... !! */
	return -1;
}

/* AHCIPortReleaseCommandSlot
 * Deallocates a previously allocated command slot */
void AhciPortReleaseCommandSlot(AhciPort_t *Port, int Slot)
{
	/* Lock access to port, we don't 
	 * want simoultanous access */
	SpinlockAcquire(&Port->Lock);

	/* Release slot */
	Port->SlotStatus &= ~(1 << Slot);

	/* Release lock */
	SpinlockRelease(&Port->Lock);
}

/* AHCIPortStartCommandSlot
 * Starts a command slot on the given port */
void AhciPortStartCommandSlot(AhciPort_t *Port, int Slot)
{
	/* Release slot */
	Port->Registers->CommandIssue |= (1 << Slot);
}

/* AHCIPortInterruptHandler
 * Port specific interrupt handler 
 * handles interrupt for a specific port */
void AhciPortInterruptHandler(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Variables */
	reg32_t DoneCommands, InterruptStatus;
	ListNode_t *tNode;
	DataKey_t Key;
	int i;
	
	/* Unused */
	_CRT_UNUSED(Controller);

	/* Store a copy of IS */
	InterruptStatus = Port->Registers->InterruptStatus;

	/* Check interrupt services 
	 * Cold port detect, recieved fis etc */


	/* Check for TFD */
	if (Port->Registers->InterruptStatus & AHCI_PORT_IE_TFEE) {
		/* Task file error */
		LogInformation("AHCI", "Port ERROR %i, CMD: 0x%x, CI 0x%x, IE: 0x%x, IS 0x%x, TFD: 0x%x", Port->Id,
			Port->Registers->CommandAndStatus, Port->Registers->CommandIssue,
			Port->Registers->InterruptEnable, Port->Registers->InterruptStatus,
			Port->Registers->TaskFileData);
	}

	/* Get completed commands */
	DoneCommands = Port->Registers->CommandIssue ^ Port->Registers->AtaActive;

	/* Check for command completion */
	if (DoneCommands) {
		/* Run through completed commands */
		for (i = 0; i < 32; i++) {
			if (DoneCommands & (1 << i)) {
				Key.Value = i;
				tNode = ListGetNodeByKey(Port->Transactions, Key, 0);
				if (tNode != NULL) {
					ListRemoveByNode(Port->Transactions, tNode);
					SchedulerWakeupAllThreads((Addr_t*)tNode);
					ListDestroyNode(Port->Transactions, tNode);
				}
			}
		}
	}

	/* Clear IS */
	Port->Registers->InterruptStatus = InterruptStatus;
}