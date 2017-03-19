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

#ifndef _AHCI_MANAGER_H_
#define _AHCI_MANAGER_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/list.h>

/* Includes
 * - System */
#include <os/driver/buffer.h>
#include "ahci.h"

/* Dispatcher Flags 
 * Used to setup transfer flags for ahci-transactions */
#define DISPATCH_MULTIPLIER(Pmp)		(Pmp & 0xF)
#define DISPATCH_WRITE					0x10
#define DISPATCH_PREFETCH				0x20
#define DISPATCH_CLEARBUSY				0x40
#define DISPATCH_ATAPI					0x80

/* AhciTransaction 
 * Describes the ahci-transaction object and contains
 * information about the buffer and the requester */
typedef struct _AhciTransaction {
	UUId_t						 Requester;
	int							 Pipe;
	BufferObject_t				*Buffer;

	AhciController_t			*Controller;
	AhciPort_t					*Port;
	AhciDevice_t				*Device;
	int							 Slot;
} AhciTransaction_t;

/* AhciCommandDispatch 
 * Dispatches a FIS command on a given port 
 * This function automatically allocates everything neccessary
 * for the transfer */
__EXTERN
OsStatus_t
AhciCommandDispatch(
	_In_ AhciTransaction_t *Transaction,
	_In_ Flags_t Flags,
	_In_ void *Command, _In_ size_t CommandLength,
	_In_ void *AtapiCmd, _In_ size_t AtapiCmdLength);


/* AhciCommandFinish
 * Verifies and cleans up a transaction made by dispatch */
__EXTERN
OsStatus_t
AhciCommandFinish(
	_In_ AhciTransaction_t *Transaction);


#endif //!_AHCI_MANAGER_H_
