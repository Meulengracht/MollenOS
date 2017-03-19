/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * TODO:
 *	- Port Multiplier Support
 *	- Power Management
 */

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/thread.h>
#include <os/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

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
	memset(Device, 0, sizeof(MCoreDevice_t));

	/* Setup information */
	Device->VendorId = 0x8086;
	Device->DeviceId = 0x0;
	Device->Class = DEVICEMANAGER_LEGACY_CLASS;
	Device->Subclass = 0x00000018;

	Device->IrqLine = -1;
	Device->IrqPin = -1;
	Device->IrqAvailable[0] = -1;

	/* Type */
	Device->Type = DeviceStorage;
	Device->Data = Disk;

	/* Initial */
	Device->Driver.Name = (char*)GlbAhciDriveDriverName;
	Device->Driver.Version = 1;
	Device->Driver.Data = AhciDisk;
	Device->Driver.Status = DriverActive;

	/* Register Device */
	DmCreateDevice("AHCI Disk Drive", Device);
}

/* AHCIReadSectors 
 * The wrapper function for reading data from an 
 * ahci-drive. It also auto-selects the command needed and everything.
 * Should return 0 on no error */
int AhciReadSectors(void *mDevice, uint64_t StartSector, void *Buffer, size_t BufferLength)
{
	/* Variables, we 
	 * need to cast some of the information */
	MCoreDevice_t *Device = (MCoreDevice_t*)mDevice;
	AhciDevice_t *AhciDisk = (AhciDevice_t*)Device->Driver.Data;
	ATACommandType_t Command;
	OsStatus_t Status;

	/* Use DMA commands? */
	if (AhciDisk->UseDMA) {
		/* Yes, decide on LBA28 or LBA48 */
		if (AhciDisk->AddressingMode == 2) {
			Command = AtaDMAReadExt;
		}
		else {
			Command = AtaDMARead;
		}
	}
	else {
		/* Nope, stick to PIO, decide on LBA28 or LBA48 */
		if (AhciDisk->AddressingMode == 2) {
			Command = AtaPIOReadExt;
		}
		else {
			Command = AtaPIORead;
		}
	}

	/* Run command */
	Status = AhciCommandRegisterFIS(AhciDisk->Controller, AhciDisk->Port,
		Command, StartSector, DIVUP(BufferLength, AhciDisk->SectorSize), 0, 0, 
		AhciDisk->AddressingMode, Buffer, BufferLength);

	/* So, how did it go? */
	if (Status != OsNoError) {
		LogFatal("AHCI", "AhciReadSectors:: Failed to do the read");
		return -1;
	}

	/* Done! */
	return 0;
}

/* AHCIWriteSectors 
 * The wrapper function for writing data to an 
 * ahci-drive. It also auto-selects the command needed and everything.
 * Should return 0 on no error */
int AhciWriteSectors(void *mDevice, uint64_t StartSector, void *Buffer, size_t BufferLength)
{
	/* Variables, we
	* need to cast some of the information */
	MCoreDevice_t *Device = (MCoreDevice_t*)mDevice;
	AhciDevice_t *AhciDisk = (AhciDevice_t*)Device->Driver.Data;
	ATACommandType_t Command;
	OsStatus_t Status;

	/* Use DMA commands? */
	if (AhciDisk->UseDMA) {
		/* Yes, decide on LBA28 or LBA48 */
		if (AhciDisk->AddressingMode == 2) {
			Command = AtaDMAWriteExt;
		}
		else {
			Command = AtaDMAWrite;
		}
	}
	else {
		/* Nope, stick to PIO, decide on LBA28 or LBA48 */
		if (AhciDisk->AddressingMode == 2) {
			Command = AtaPIOWriteExt;
		}
		else {
			Command = AtaPIOWrite;
		}
	}

	/* Run command */
	Status = AhciCommandRegisterFIS(AhciDisk->Controller, AhciDisk->Port,
		Command, StartSector, DIVUP(BufferLength, AhciDisk->SectorSize), 0, 1,
		AhciDisk->AddressingMode, Buffer, BufferLength);

	/* So, how did it go? */
	if (Status != OsNoError) {
		LogFatal("AHCI", "AhciWriteSectors:: Failed to write");
		return -1;
	}

	return 0;
}
