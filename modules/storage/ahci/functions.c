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
#include <os/utils.h>
#include "manager.h"
#include <stdlib.h>

/* AhciReadSectors 
 * The wrapper function for reading data from an 
 * ahci-drive. It also auto-selects the command needed and everything.
 * Should return 0 on no error */
OsStatus_t
AhciReadSectors(
	_In_ AhciTransaction_t* Transaction, 
	_In_ uint64_t           SectorLBA)
{
	ATACommandType_t Command;

	// Sanitize bounds
	if ((SectorLBA + Transaction->SectorCount) >= Transaction->Device->SectorsLBA) {
		return OsError;
	}

	// Sanitize requested sector-count
	if (Transaction->SectorCount == 0) {
		return OsError;
	}

	// The first thing we need to do is determine which type
	// of ATA command we can use
	if (Transaction->Device->UseDMA) {
		if (Transaction->Device->AddressingMode == 2) {
			Command = AtaDMAReadExt; // LBA48
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
	return AhciCommandRegisterFIS(Transaction, Command, SectorLBA, 0, 0);
}

/* AhciWriteSectors 
 * The wrapper function for writing data to an 
 * ahci-drive. It also auto-selects the command needed and everything.
 * Should return 0 on no error */
OsStatus_t
AhciWriteSectors(
	_In_ AhciTransaction_t* Transaction,
	_In_ uint64_t           SectorLBA)
{
	ATACommandType_t Command;

	// Sanitize bounds
	if ((SectorLBA + Transaction->SectorCount) >= Transaction->Device->SectorsLBA) {
		return OsError;
	}

	// Sanitize requested sector-count
	if (Transaction->SectorCount == 0) {
		return OsError;
	}

	// The first thing we need to do is determine which type
	// of ATA command we can use
	if (Transaction->Device->UseDMA) {
		if (Transaction->Device->AddressingMode == 2) {
			Command = AtaDMAWriteExt;	// LBA48
		}
		else {
			Command = AtaDMAWrite;	// LBA28
		}
	}
	else {
		if (Transaction->Device->AddressingMode == 2) {
			Command = AtaPIOWriteExt;	// LBA48
		}
		else {
			Command = AtaPIOWrite;	// LBA28
		}
	}
	return AhciCommandRegisterFIS(Transaction, Command, SectorLBA, 0, 1);
}
