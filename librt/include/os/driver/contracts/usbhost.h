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
#include <os/driver/usb.h>
#include <os/osdefs.h>

/* Usb host controller query functions that must be implemented
 * by the usb host driver - those can then be used by this interface */
#define __USBHOST_QUEUETRANSFER		IPC_DECL_FUNCTION(0)
#define __USBHOST_QUEUEPERIODIC		IPC_DECL_FUNCTION(1)
#define __USBHOST_DEQUEUEPERIODIC	IPC_DECL_FUNCTION(2)

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

/* UsbTransferStatus
 * Describes a unified way of reporting how a transfer ended.
 * Where the initial state is NotProcessed */
typedef enum _UsbTransferStatus {
	TransferNotProcessed,
	TransferFinished,
	TransferStalled,
	TransferNotResponding,
	TransferInvalidToggles,
	TransferInvalidData,
	TransferNAK,
	TransferBabble
} UsbTransferStatus_t;

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
	// Generic Information
	UsbTransferType_t					Type;
	UsbSpeed_t							Speed;
	UsbTransaction_t					Transactions[3];

	// Endpoint Information
	UsbEndpointDescriptor_t				Endpoint;

	// Periodic Information
	__CONST void*						PeriodicData;
});

/* UsbQueueTransfer 
 * Queues a new Control or Bulk transfer for the given driver
 * and pipe. They must exist. The function blocks untill execution */
SERVICEAPI
OsStatus_t
SERVICEABI
UsbQueueTransfer(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Pipe,
	_In_ UsbTransfer_t *Transfer)
{
	// Variables
	UsbTransferStatus_t Status = TransferNotProcessed;
	MContract_t Contract;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_QUEUETRANSFER,
		&Pipe, sizeof(int), NULL, 0, NULL, 0, 
		&Status, sizeof(UsbTransferStatus_t));
}

/* UsbQueuePeriodic 
 * Queues a new Interrupt or Isochronous transfer. This transfer is 
 * persistant untill device is disconnected or Dequeue is called. */
SERVICEAPI
OsStatus_t
SERVICEABI
UsbQueuePeriodic(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Pipe,
	_In_ UsbTransfer_t *Transfer)
{
	// Variables
	UsbTransferStatus_t Status = TransferNotProcessed;
	MContract_t Contract;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_QUEUEPERIODIC,
		&Pipe, sizeof(int), NULL, 0, NULL, 0, 
		&Status, sizeof(UsbTransferStatus_t));
}

/* UsbDequeuePeriodic */



#endif //!_CONTRACT_USBHOST_INTERFACE_H_
