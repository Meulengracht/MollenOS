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

/* AHCICommandDispatch 
 * Dispatches a FIS command on a given port 
 * This function automatically allocates everything neccessary
 * for the transfer */
OsStatus_t AhciCommandDispatch(AhciController_t *Controller, 
	AhciPort_t *Port, AhciTransaction_t *Transaction)
{
	/* Variables */
	AHCICommandTable_t *CmdTable = NULL;
	uint8_t *BufferPtr = NULL;
	size_t BytesLeft = Transaction->BufferLength;
	int Slot = 0, PrdtIndex = 0;
	DataKey_t Key;

	/* Assert that buffer is DWORD aligned */
	if (((Addr_t)Transaction->Buffer & 0x3) != 0) {
		LogFatal("AHCI", "AhciCommandDispatch::Buffer was not dword aligned", Port->Id);
		return OsError;
	}

	/* Assert that buffer length is an even byte-count requested */
	if ((Transaction->BufferLength & 0x1) != 0) {
		LogFatal("AHCI", "AhciCommandDispatch::BufferLength is odd, must be even", Port->Id);
		return OsError;
	}

	/* Allocate a slot for this FIS */
	Slot = AhciPortAcquireCommandSlot(Controller, Port);

	/* Sanitize that there is room 
	 * for our command */
	if (Slot < 0) {
		LogFatal("AHCI", "Port %u was out of command slots!!", Port->Id);
		return OsError;
	}

	/* Store slot */
	Transaction->Slot = Slot;

	/* Get a reference to the command slot */
	CmdTable = (AHCICommandTable_t*)Port->CommandList->Headers[Slot].CmdTableBaseAddress;

	/* Zero out the command table */
	memset(CmdTable, 0, AHCI_COMMAND_TABLE_SIZE);

	/* Sanitizie packet lenghts */
	if (Transaction->CommandLength > 64
		|| Transaction->AtapiCmdLength > 16) {
		LogFatal("AHCI", "Commands are exceeding the allowed length, FIS (%u), ATAPI (%u)", 
			Transaction->CommandLength, Transaction->AtapiCmdLength);
		goto Error;
	}

	/* Copy data over to packet */
	memcpy(&CmdTable->FISCommand[0], Transaction->Command, Transaction->CommandLength);
	memcpy(&CmdTable->FISAtapi[0], Transaction->AtapiCmd, Transaction->AtapiCmdLength);

	/* Build PRDT */
	BufferPtr = (uint8_t*)Transaction->Buffer;
	while (BytesLeft > 0)
	{
		/* Get handler to prdt entry */
		AHCIPrdtEntry_t *Prdt = &CmdTable->PrdtEntry[PrdtIndex];

		/* Calculate how much to transfer */
		size_t TransferLength = MIN(AHCI_PRDT_MAX_LENGTH, BytesLeft);

		/* Get the physical address of buffer */
		Addr_t PhysicalAddr = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)BufferPtr);

		/* Set buffer */
		Prdt->DataBaseAddress = LODWORD(PhysicalAddr);
		Prdt->DataBaseAddressUpper = (sizeof(void*) > 4) ? HIDWORD(PhysicalAddr) : 0;

		/* Set transfer length */
		Prdt->Descriptor = TransferLength - 1;

		/* Adjust pointer and length */
		BytesLeft -= TransferLength;
		BufferPtr += TransferLength;
		PrdtIndex++;

		/* Set IOC on last */
		if (BytesLeft == 0) {
			Prdt->Descriptor |= AHCI_PRDT_IOC;
		}
	}

	/* Update command table */
	Port->CommandList->Headers[Slot].TableLength = (uint16_t)PrdtIndex;
	Port->CommandList->Headers[Slot].Flags = 
		(uint16_t)(Transaction->Write << 6) | (uint16_t)(Transaction->CommandLength / 4);

	/* Add transaction to queue */
	Key.Value = Slot;
	ListAppend(Port->Transactions, ListCreateNode(Key, Key, Transaction));

	/* Start command */
	AhciPortStartCommandSlot(Port, Slot);

	/* Start the sleep */
	SchedulerSleepThread((Addr_t*)Transaction, 0);
	IThreadYield();

	/* Done */
	return OsNoError;

Error:
	/* Cleanup */
	AhciPortReleaseCommandSlot(Port, Slot);

	/* Return error */
	return OsError;
}
