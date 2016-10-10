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

	/* Done, return it! */
	return AhciPort;
}

/* AHCIPortInit 
 * Initializes the memory regions and enables them in the port */
void AhciPortInit(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Variables */
	uint8_t *CmdTablePtr = NULL;
	int i;

	/* Initialize memory structures 
	 * Both RecievedFIS and PRDT */
	Port->CommandList = (AHCICommandList_t*)((uint8_t*)Controller->CmdListBase + (1024 * Port->Id));
	Port->RecievedFis = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (256 * Port->Id));
	Port->CommandTable = (void*)((uint8_t*)Controller->CmdTableBase 
		+ (((256 * Controller->CmdSlotCount) * 32) * Port->Id));

	/* Setup mem pointer */
	CmdTablePtr = (uint8_t*)Port->CommandTable;

	/* Iterate command headers */
	for (i = 0; i < 32; i++) {
		
		/* Setup flags */
		Port->CommandList->Headers[i].Flags = 0;
		Port->CommandList->Headers[i].TableLength = 8;
		Port->CommandList->Headers[i].PRDByteCount = 0;

		/* Set command table address */
		Port->CommandList->Headers[i].CmdTableBaseAddress = LODWORD((uint32_t)CmdTablePtr);

		/* Set command table address upper register 
		 * Only set upper if we are in 64 bit */
		Port->CommandList->Headers[i].CmdTableBaseAddressUpper = 
			(sizeof(void*) > 4) ? HIDWORD(CmdTablePtr) : 0;

		/* Increament pointer */
		CmdTablePtr += (256 * Controller->CmdSlotCount);
	}

	/* Update registers */
	Port->Registers->CmdListBaseAddress = LODWORD((uint32_t)Port->CommandList);
	Port->Registers->CmdListBaseAddressUpper = (sizeof(void*) > 4) ? HIDWORD(Port->CommandList) : 0;

	Port->Registers->FISBaseAddress = LOWORD((uint32_t)Port->RecievedFis);
	Port->Registers->FISBaseAdressUpper = (sizeof(void*) > 4) ? HIDWORD(Port->RecievedFis) : 0;

	/* After setting PxFB and PxFBU to the physical address of the FIS receive area,
	 * system software shall set PxCMD.FRE to ‘1’. */
	Port->Registers->CommandAndStatus |= AHCI_PORT_FRE;

	/* For each implemented port, clear the PxSERR register, 
	 * by writing ‘1s’ to each implemented bit location. */
	Port->Registers->AtaError = AHCI_PORT_SERR_CLEARALL;
	Port->Registers->InterruptStatus = 0xFFFFFFFF;
	
	/* Determine which events should cause an interrupt, 
	 * and set each implemented port’s PxIE register with the appropriate enables. */
	Port->Registers->InterruptEnable = 
		(uint32_t)(AHCI_PORT_IE_CPDE | AHCI_PORT_IE_DSE | AHCI_PORT_IE_PSE | AHCI_PORT_IE_DHRE);
}

/* AHCIPortCleanup
 * Destroys a port, cleans up device, cleans up memory and resources */
void AhciPortCleanup(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Set it null in the controller */
	Controller->Ports[Port->Index] = NULL;

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
	/* Unused */
	_CRT_UNUSED(Controller);

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
	Port->Connected = 1;

	/* Query device */

}

/* AHCIPortInterruptHandler
 * Port specific interrupt handler 
 * handles interrupt for a specific port */
void AhciPortInterruptHandler(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Unused */
	_CRT_UNUSED(Controller);
	_CRT_UNUSED(Port);
}