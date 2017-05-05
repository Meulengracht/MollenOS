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
 * MollenOS MCore - Usb Definitions & Structures
 * - This header describes the base usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _USB_INTERFACE_H_
#define _USB_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/usb/definitions.h>
#include <os/driver/service.h>
#include <os/driver/buffer.h>
#include <os/ipc/ipc.h>

/* UsbSpeed 
 * Describes the possible speeds for usb devices */
typedef enum _UsbSpeed {
	LowSpeed,		// 1.0 / 1.1
	FullSpeed,		// 1.0 / 1.1 / 2.0 (HID)
	HighSpeed,		// 2.0
	SuperSpeed		// 3.0
} UsbSpeed_t;

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
	uintptr_t							BufferAddress;
	size_t								Length;
});

/* UsbTransfer 
 * Describes an usb-transfer, that consists of transfer information
 * and a bunch of transactions. */
PACKED_TYPESTRUCT(UsbTransfer, {
	UsbTransferType_t					Type;
});

/* UsbPortDescriptor 
 * Describes the current port information */
PACKED_TYPESTRUCT(UsbPortDescriptor, {
	UsbSpeed_t							Speed;
	int									Enabled;
	int									Connected;
});

/* UsbControllerRegister
 * Registers a new controller with the given type and setup */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbControllerRegister(
	_In_ void *Data,
	_In_ int Type,
	_In_ size_t Ports);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbControllerRegister(
	_In_ void *Data,
	_In_ int Type,
	_In_ size_t Ports)
{

}
#endif

/* UsbControllerUnregister
 * Unregisters the given usb-controller from the manager and
 * unregisters any devices registered by the controller */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbControllerUnregister(
	_In_ UUId_t Controller);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbControllerUnregister(
	_In_ UUId_t Controller)
{

}
#endif

/* UsbEventPort */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbEventPort(
	_In_ UUId_t Controller);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbEventPort(
	_In_ UUId_t Controller,
	_In_ int Index)
{

}
#endif

#endif //!_USB_INTERFACE_H_
