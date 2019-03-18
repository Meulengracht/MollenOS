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

#include <commands.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>

OsStatus_t
AhciReadSectors(
	_In_ AhciTransaction_t* Transaction, 
	_In_ uint64_t           SectorLBA)
{
    size_t           SectorsToBeRead;
	ATACommandType_t Command;

    // Protect against bad start sector
    if (SectorLBA >= Transaction->Device->SectorsLBA ||
    	Transaction->SectorCount == 0) {
        return OsInvalidParameters;
    }

    // Of course it's possible that the requester is requesting too much data in one
    // go, so we will have to clamp some of the values. Is the sector valid first of all?
    SectorsToBeRead = Transaction->SectorCount;
    if ((SectorLBA + SectorsToBeRead) >= Transaction->Device->SectorsLBA) {
        SectorsToBeRead = Transaction->Device->SectorsLBA - SectorLBA;
    }

	// The first thing we need to do is determine which type
	// of ATA command we can use. The number of sectors that can be read at once
	// is pretty limited unless the addressing mode is set to extended (2).
	SectorsToBeRead = MIN(Transaction->SectorCount, UINT8_MAX);
	if (Transaction->Device->UseDMA) {
		if (Transaction->Device->AddressingMode == 2) {
			Command         = AtaDMAReadExt; // LBA48
			SectorsToBeRead = MIN(Transaction->SectorCount, UINT16_MAX);
		}
		else {
			Command = AtaDMARead; // LBA28
		}
	}
	else {
		if (Transaction->Device->AddressingMode == 2) {
			Command = AtaPIOReadExt; // LBA48
		}
		else {
			Command = AtaPIORead; // LBA28
		}
	}
	
	// Update the number of sectors we want to read
	Transaction->SectorCount = SectorsToBeRead;
	return AhciCommandRegisterFIS(Transaction, Command, SectorLBA, 0, 0);
}

OsStatus_t
AhciWriteSectors(
	_In_ AhciTransaction_t* Transaction,
	_In_ uint64_t           SectorLBA)
{
    size_t           SectorsToBeWritten;
	ATACommandType_t Command;

    // Protect against bad start sector
    if (SectorLBA >= Transaction->Device->SectorsLBA ||
    	Transaction->SectorCount == 0) {
        return OsInvalidParameters;
    }

    // Of course it's possible that the requester is requesting too much data in one
    // go, so we will have to clamp some of the values. Is the sector valid first of all?
    SectorsToBeWritten = Transaction->SectorCount;
    if ((SectorLBA + SectorsToBeWritten) >= Transaction->Device->SectorsLBA) {
        SectorsToBeWritten = Transaction->Device->SectorsLBA - SectorLBA;
    }

	// The first thing we need to do is determine which type
	// of ATA command we can use. The number of sectors that can be read at once
	// is pretty limited unless the addressing mode is set to extended (2).
	SectorsToBeWritten = MIN(Transaction->SectorCount, UINT8_MAX);
	if (Transaction->Device->UseDMA) {
		if (Transaction->Device->AddressingMode == 2) {
			Command            = AtaDMAWriteExt; // LBA48
			SectorsToBeWritten = MIN(Transaction->SectorCount, UINT16_MAX);
		}
		else {
			Command = AtaDMAWrite; // LBA28
		}
	}
	else {
		if (Transaction->Device->AddressingMode == 2) {
			Command = AtaPIOWriteExt; // LBA48
		}
		else {
			Command = AtaPIOWrite; // LBA28
		}
	}
	
	// Update the number of sectors we want to write
	Transaction->SectorCount = SectorsToBeWritten;
	return AhciCommandRegisterFIS(Transaction, Command, SectorLBA, 0, 1);
}
