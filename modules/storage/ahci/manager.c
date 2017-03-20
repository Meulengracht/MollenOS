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
#include <os/driver/file.h>
#include <os/mollenos.h>
#include <os/thread.h>
#include <os/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

/* AHCIStringFlip 
 * Flips a string returned by an ahci command
 * so it's readable */
void
AhciStringFlip(
	_In_ uint8_t *Buffer,
	_In_ size_t Length)
{
	// Variables
	size_t StringPairs = Length / 2;
	size_t i;

	// Iterate pairs in string, and swap
	for (i = 0; i < StringPairs; i++) {
		uint8_t TempChar = Buffer[i * 2];
		Buffer[i * 2] = Buffer[i * 2 + 1];
		Buffer[i * 2 + 1] = TempChar;
	}
}

/* AhciManagerInitialize
 * Initializes the ahci manager that keeps track of
 * all controllers and all attached devices */
OsStatus_t
AhciManagerInitialize(void)
{

}

/* AhciManagerDestroy
 * Cleans up the manager and releases resources allocated */
OsStatus_t
AhciManagerDestroy(void)
{

}

/* AhciManagerCreateDevice
 * Registers a new device with the ahci-manager on the specified
 * port and controller. Identifies and registers with neccessary services */
OsStatus_t
AhciManagerCreateDevice(
	_In_ AhciController_t *Controller, 
	_In_ AhciPort_t *Port)
{
	// Structures
	AhciTransaction_t *Transaction = NULL;
	BufferObject_t *Buffer = NULL;
	AhciDevice_t *Device = NULL;

	// First of all, is this a port multiplier? 
	// because then we should really enumerate it
	if (Port->Registers->Signature == SATA_SIGNATURE_PM
		|| Port->Registers->Signature == SATA_SIGNATURE_SEMB) {
		MollenOSSystemLog("AHCI::Unsupported device type 0x%x on port %i",
			Port->Registers->Signature, Port->Id);
		return OsError;
	}

	// Allocate data-structures
	Transaction = (AhciTransaction_t*)malloc(sizeof(AhciTransaction_t));
	Device = (AhciDevice_t*)malloc(sizeof(AhciDevice_t));
	Buffer = CreateBuffer(sizeof(ATAIdentify_t));

	// Initiate a new device structure
	Device->Controller = Controller;
	Device->Port = Port;
	Device->Buffer = Buffer;

	// Initiate the transaction
	Transaction->Requester = UUID_INVALID;
	Transaction->Address = Buffer->Physical;
	Transaction->SectorCount = 1;
	Transaction->Device = Device;

	// Ok, so either ATA or ATAPI
	return AhciCommandRegisterFIS(Transaction, AtaPIOIdentifyDevice, 0, 0, 0);
}

/* AhciManagerCreateDeviceCallback
 * Needs to be called once the identify command has
 * finished executing */
OsStatus_t
AhciManagerCreateDeviceCallback(
	_In_ AhciDevice_t *Device)
{
	// Variables
	ATAIdentify_t *DeviceInformation;

	// Instantiate pointer
	DeviceInformation = (ATAIdentify_t*)Device->Buffer->Virtual;

	// Flip the data in the strings as it's inverted
	AhciStringFlip(DeviceInformation->SerialNo, 20);
	AhciStringFlip(DeviceInformation->ModelNo, 40);
	AhciStringFlip(DeviceInformation->FWRevision, 8);

	// Determine device type
	if (Device->Port->Registers->Signature == SATA_SIGNATURE_ATAPI) {
		Device->Type = 1;
	}
	else {
		Device->Type = 0;
	}

	// Set capabilities
	if (DeviceInformation->Capabilities0 & (1 << 0)) {
		Device->UseDMA = 1;
	}

	// Check addressing mode supported
	// Check that LBA is supported
	if (DeviceInformation->Capabilities0 & (1 << 1)) {
		Device->AddressingMode = 1; // LBA28
		if (DeviceInformation->CommandSetSupport1 & (1 << 10)) {
			Device->AddressingMode = 2; // LBA48
		}
	}
	else {
		Device->AddressingMode = 0; // CHS
	}

	// Calculate sector size if neccessary
	if (DeviceInformation->SectorSize & (1 << 12)) {
		Device->SectorSize = DeviceInformation->WordsPerLogicalSector * 2;
	}
	else {
		Device->SectorSize = 512;
	}

	/* Calculate sector count per physical sector */
	if (DeviceInformation->SectorSize & (1 << 13)) {
		Device->SectorSize *= (DeviceInformation->SectorSize & 0xF);
	}

	/* Now, get the number of sectors for
	* this particular disk */
	if (DeviceInformation->SectorCountLBA48 != 0) {
		Device->SectorsLBA = DeviceInformation->SectorCountLBA48;
	}
	else {
		Device->SectorsLBA = DeviceInformation->SectorCountLBA28;
	}

	// At this point the ahcidisk structure is filled
	// and we can continue to fill out the mcoredisk */
	Disk->Manufactor = NULL;
	Disk->ModelNo = strndup((const char*)&DeviceInformation.ModelNo[0], 40);
	Disk->Revision = strndup((const char*)&DeviceInformation.FWRevision[0], 8);
	Disk->SerialNo = strndup((const char*)&DeviceInformation.SerialNo[0], 20);





	// Lastly register disk
	return RegisterDisk(0, 0, __DISK_REMOVABLE);
}
