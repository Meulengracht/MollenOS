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
 * MollenOS MCore - Contract Definitions & Structures (Disk Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_USBHOST_INTERFACE_H_
#define _CONTRACT_USBHOST_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/usb/definitions.h>
#include <os/driver/driver.h>
#include <os/osdefs.h>

/* Usb host controller query functions that must be implemented
 * by the usb host driver - those can then be used by this interface */
#define __USBHOST_QUEUE				IPC_DECL_FUNCTION(0)
#define __USBHOST_WAIT				IPC_DECL_FUNCTION(1)
#define __USBHOST_DEQUEUE			IPC_DECL_FUNCTION(2)

/* UsbTransactionType 
 * Describes the possible types of usb transactions */
typedef enum _UsbTransactionType {
	SetupTransaction,
	InTransaction,
	OutTransaction
} UsbTransactionType_t;

/* UsbTransferType 
 * Describes the type of transfer, it can be one of 4 that describe
 * either Control, Bulk, Interrupt or Isochronous */
typedef enum _UsbTransferType {
	ControlTransfer,
	BulkTransfer,
	InterruptTransfer,
	IsochronousTransfer
} UsbTransferType_t;

/* UsbTransaction
 * Describes a single transaction in an usb-transfer operation */
PACKED_TYPESTRUCT(UsbTransaction, {
	UsbTransactionType_t				Type;

	// Data Information
	uintptr_t							BufferAddress;
	size_t								Length;
});

/* UsbTransfer 
 * Describes an usb-transfer, that consists of transfer information
 * and a bunch of transactions. */
PACKED_TYPESTRUCT(UsbTransfer, {
	UsbTransferType_t					Type;
	UsbTransaction_t					Transactions[3];
});

/* UsbQueueTransfer */

/* UsbWaitTransfer */

/* UsbDequeueTransfer */



#endif //!_CONTRACT_USBHOST_INTERFACE_H_
