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
#include <Devices/Disk.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>

/* CLib */
#include <stddef.h>
#include <string.h>

/* AHCICommandDispatch Flags 
 * Used to setup transfer flags */
#define DISPATCH_MULTIPLIER(Pmp)		(Pmp & 0xF)
#define DISPATCH_WRITE					0x10
#define DISPATCH_PREFETCH				0x20
#define DISPATCH_CLEARBUSY				0x40
#define DISPATCH_ATAPI					0x80

/* Prototypes 
 * Read/Write forwarding for our setup */
int AhciReadSectors(void *mDevice, uint64_t StartSector, void *Buffer, size_t BufferLength);
int AhciWriteSectors(void *mDevice, uint64_t StartSector, void *Buffer, size_t BufferLength);

/* AHCIStringFlip 
 * Flips a string returned by an ahci command
 * so it's readable */
void AhciStringFlip(uint8_t *Buffer, size_t Length)
{
	/* Variables */
	size_t StringPairs = Length / 2;
	size_t i;

	/* Iterate pairs in string, and swap */
	for (i = 0; i < StringPairs; i++)
	{
		/* Get temporary character */
		uint8_t TempChar = Buffer[i * 2];

		/* Do the swap */
		Buffer[i * 2] = Buffer[i * 2 + 1];
		Buffer[i * 2 + 1] = TempChar;
	}
}

/* AHCICommandDispatch 
 * Dispatches a FIS command on a given port 
 * This function automatically allocates everything neccessary
 * for the transfer */
OsStatus_t AhciCommandDispatch(AhciController_t *Controller, AhciPort_t *Port, uint32_t Flags,
	void *Command, size_t CommandLength, void *AtapiCmd, size_t AtapiCmdLength, 
	void *Buffer, size_t BufferLength)
{
	/* Variables */
	AHCICommandTable_t *CmdTable = NULL;
	uint8_t *BufferPtr = NULL;
	size_t BytesLeft = BufferLength;
	int Slot = 0, PrdtIndex = 0;
	ListNode_t *tNode = NULL;
	DataKey_t Key, SubKey;

	/* Assert that buffer is DWORD aligned */
	if (((Addr_t)Buffer & 0x3) != 0) {
		LogFatal("AHCI", "AhciCommandDispatch::Buffer was not dword aligned", Port->Id);
		return OsError;
	}

	/* Assert that buffer length is an even byte-count requested */
	if ((BufferLength & 0x1) != 0) {
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

	/* Get a reference to the command slot */
	CmdTable = (AHCICommandTable_t*)((uint8_t*)Port->CommandTable + (AHCI_COMMAND_TABLE_SIZE * Slot));

	/* Zero out the command table */
	memset(CmdTable, 0, AHCI_COMMAND_TABLE_SIZE);

	/* Sanitizie packet lenghts */
	if (CommandLength > 64
		|| AtapiCmdLength > 16) {
		LogFatal("AHCI", "Commands are exceeding the allowed length, FIS (%u), ATAPI (%u)", 
			CommandLength, AtapiCmdLength);
		goto Error;
	}

	/* Copy data over to packet */
	if (Command != NULL)
		memcpy(&CmdTable->FISCommand[0], Command, CommandLength); 
	if (AtapiCmd != NULL)
		memcpy(&CmdTable->FISAtapi[0], AtapiCmd, AtapiCmdLength);

	/* Build PRDT */
	BufferPtr = (uint8_t*)Buffer;
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
	Port->CommandList->Headers[Slot].Flags = (uint16_t)(CommandLength / 4);

	/* Set flags */
	if (Flags & DISPATCH_ATAPI)
		Port->CommandList->Headers[Slot].Flags |= (1 << 5);
	if (Flags & DISPATCH_WRITE)
		Port->CommandList->Headers[Slot].Flags |= (1 << 6);
	if (Flags & DISPATCH_PREFETCH)
		Port->CommandList->Headers[Slot].Flags |= (1 << 7);
	if (Flags & DISPATCH_CLEARBUSY)
		Port->CommandList->Headers[Slot].Flags |= (1 << 10);

	/* Update PMP */
	Port->CommandList->Headers[Slot].Flags |= (DISPATCH_MULTIPLIER(Flags) << 12);

	/* Setup Keys */
	Key.Value = Slot;
	SubKey.Value = (int)DISPATCH_MULTIPLIER(Flags);

	/* Add transaction to queue */
	tNode = ListCreateNode(Key, SubKey, NULL);
	ListAppend(Port->Transactions, tNode);

	/* Enter critical section 
	 * We do this since it's possible for the
	 * port interrupt to happen before we sleep
	 * our self on the resource */
	CriticalSectionEnter(&Port->Section);

	/* Start command */
	AhciPortStartCommandSlot(Port, Slot);

	/* Wait for signal to happen on resource */
	SchedulerSleepThread((Addr_t*)tNode, 0);

	/* Leave critical section, and allow 
	 * it to be interrupt, then yield */
	CriticalSectionLeave(&Port->Section);
	IThreadYield();

	/* Done */
	return OsNoError;

Error:
	/* Cleanup */
	AhciPortReleaseCommandSlot(Port, Slot);

	/* Return error */
	return OsError;
}

/* AHCICommandRegisterFIS 
 * Builds a new AHCI Transaction based on a register FIS */
OsStatus_t AhciCommandRegisterFIS(AhciController_t *Controller, AhciPort_t *Port, 
	ATACommandType_t Command, uint64_t SectorLBA, size_t SectorCount, int Device, 
	int Write, int AddressingMode, void *Buffer, size_t BufferSize)
{
	/* Variables */
	FISRegisterH2D_t Fis;
	uint32_t Flags;

	/* Reset structure */
	memset((void*)&Fis, 0, sizeof(FISRegisterH2D_t));

	/* Fill in FIS */
	Fis.FISType = LOBYTE(FISRegisterH2D);
	Fis.Flags |= FIS_HOST_TO_DEVICE;
	Fis.Command = LOBYTE(Command);
	Fis.Device = 0x40 | ((LOBYTE(Device) & 0x1) << 4);

	/* Set CHS fields */
	if (AddressingMode == 0) 
	{
		/* Variables */
		//uint16_t Head = 0, Cylinder = 0, Sector = 0;

		/* Step 1 -> Transform LBA into CHS */

		/* Set CHS params */



		/* Set count */
		Fis.Count = LOBYTE(SectorCount);
	}
	else if (AddressingMode == 1
		|| AddressingMode == 2) {
		/* Set LBA28 params */
		Fis.SectorNo = LOBYTE(SectorLBA);
		Fis.SectorNoExtended = (uint8_t)((SectorLBA >> 8) & 0xFF);
		Fis.CylinderLow = (uint8_t)((SectorLBA >> 16) & 0xFF);
		Fis.CylinderLowExtended = (uint8_t)((SectorLBA >> 24) & 0xFF);

		/* If it's an LBA48, set LBA48 params */
		if (AddressingMode == 2) {
			Fis.CylinderHigh = (uint8_t)((SectorLBA >> 32) & 0xFF);
			Fis.CylinderHighExtended = (uint8_t)((SectorLBA >> 40) & 0xFF);

			/* Set count */
			Fis.Count = LOWORD(SectorCount);
		}
		else {
			/* Set count */
			Fis.Count = LOBYTE(SectorCount);
		}
	}

	/* Build flags */
	Flags = DISPATCH_MULTIPLIER(0);
	
	/* Is this an ATAPI? */
	if (Port->Registers->Signature == SATA_SIGNATURE_ATAPI) {
		Flags |= DISPATCH_ATAPI;
	}

	/* Write operation? */
	if (Write != 0) {
		Flags |= DISPATCH_WRITE;
	}

	/* Execute our command */
	return AhciCommandDispatch(Controller, Port, Flags, &Fis,
		sizeof(FISRegisterH2D_t), NULL, 0, Buffer, BufferSize);
}

/* AHCIDeviceIdentify 
 * Identifies the device and type on a port
 * and sets it up accordingly */
void AhciDeviceIdentify(AhciController_t *Controller, AhciPort_t *Port)
{
	/* Variables */
	MCoreStorageDevice_t *Disk;
	MCoreDevice_t *Device;
	ATAIdentify_t DeviceInformation;
	AhciDevice_t *AhciDisk;
	OsStatus_t Status;

	/* First of all, is this a port multiplier? 
	 * because then we should really enumerate it */
	if (Port->Registers->Signature == SATA_SIGNATURE_PM
		|| Port->Registers->Signature == SATA_SIGNATURE_SEMB) {
		LogDebug("AHCI", "Unsupported device type 0x%x on port %i",
			Port->Registers->Signature, Port->Id);
		return;
	}

	/* Ok, so either ATA or ATAPI */
	Status = AhciCommandRegisterFIS(Controller, Port, AtaPIOIdentifyDevice,
		0, 0, 0, 0, -1, (void*)&DeviceInformation, sizeof(ATAIdentify_t));

	/* So, how did it go? */
	if (Status != OsNoError) {
		LogFatal("AHCI", "AHCIDeviceIdentify:: Failed to send Identify");
		return;
	}

	/* Flip the strings */
	AhciStringFlip(DeviceInformation.SerialNo, 20);
	AhciStringFlip(DeviceInformation.ModelNo, 40);
	AhciStringFlip(DeviceInformation.FWRevision, 8);

	/* Allocate the disk device 
	 * we need it to register a new disk */
	Disk = (MCoreStorageDevice_t*)kmalloc(sizeof(MCoreStorageDevice_t));
	AhciDisk = (AhciDevice_t*)kmalloc(sizeof(AhciDevice_t));
	Device = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));

	/* Set initial stuff */
	AhciDisk->Controller = Controller;
	AhciDisk->Port = Port;

	/* Set capabilities */
	if (DeviceInformation.Capabilities0 & (1 << 0)) {
		AhciDisk->UseDMA = 1;
	}

	if (Port->Registers->Signature == SATA_SIGNATURE_ATAPI) {
		AhciDisk->DeviceType = 1;
	}
	else {
		AhciDisk->DeviceType = 0;
	}

	/* Check addressing mode supported 
	 * Check that LBA is supported */
	if (DeviceInformation.Capabilities0 & (1 << 1)) {
		AhciDisk->AddressingMode = 1;

		/* Is LBA48 commands supported? */
		if (DeviceInformation.CommandSetSupport1 & (1 << 10)) {
			AhciDisk->AddressingMode = 2;
		}
	}
	else {
		AhciDisk->AddressingMode = 0;
	}

	/* Calculate sector size if neccessary */
	if (DeviceInformation.SectorSize & (1 << 12)) {
		AhciDisk->SectorSize = DeviceInformation.WordsPerLogicalSector * 2;
	}
	else {
		AhciDisk->SectorSize = 512;
	}
	
	/* Calculate sector count per physical sector */
	if (DeviceInformation.SectorSize & (1 << 13)) {
		AhciDisk->SectorSize *= (DeviceInformation.SectorSize & 0xF);
	}

	/* Now, get the number of sectors for 
	 * this particular disk */
	if (DeviceInformation.SectorCountLBA48 != 0) {
		AhciDisk->SectorsLBA = DeviceInformation.SectorCountLBA48;
	}
	else {
		AhciDisk->SectorsLBA = DeviceInformation.SectorCountLBA28;
	}

	/* At this point the ahcidisk structure is filled
	 * and we can continue to fill out the mcoredisk */
	Disk->Manufactor = NULL;
	Disk->ModelNo = strndup((const char*)&DeviceInformation.ModelNo[0], 40);
	Disk->Revision = strndup((const char*)&DeviceInformation.FWRevision[0], 8);
	Disk->SerialNo = strndup((const char*)&DeviceInformation.SerialNo[0], 20);

	Disk->SectorCount = AhciDisk->SectorsLBA;
	Disk->SectorSize = AhciDisk->SectorSize;

	/* Setup functions */
	Disk->Read = AhciReadSectors;
	Disk->Write = AhciWriteSectors;

	/* At this point all the disk structures are filled 
	 * out, and we can build a MCoreDevice_t under our controller
	 * as parent */
	

	/* Transform information */
	LogInformation("AHCI", "Drive Model: %s, SectorCount 0x%x", 
		Disk->ModelNo, DeviceInformation.SectorCountLBA28);
}

int AhciReadSectors(void *mDevice, uint64_t StartSector, void *Buffer, size_t BufferLength)
{
	_CRT_UNUSED(mDevice);
	_CRT_UNUSED(StartSector);
	_CRT_UNUSED(Buffer);
	_CRT_UNUSED(BufferLength);
	return 0;
}

int AhciWriteSectors(void *mDevice, uint64_t StartSector, void *Buffer, size_t BufferLength)
{
	_CRT_UNUSED(mDevice);
	_CRT_UNUSED(StartSector);
	_CRT_UNUSED(Buffer);
	_CRT_UNUSED(BufferLength);
	return 0;
}