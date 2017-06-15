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

/* These definitions are in-place to allow a custom
 * setting of the device-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __USBMANAGER_INTERFACE_VERSION		1

/* These are the different IPC functions supported
 * by the usbmanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __USBMANAGER_REGISTERCONTROLLER			IPC_DECL_FUNCTION(0)
#define __USBMANAGER_UNREGISTERCONTROLLER		IPC_DECL_FUNCTION(1)
#define __USBMANAGER_QUERYCONTROLLERS			IPC_DECL_FUNCTION(2)

#define __USBMANAGER_PORTEVENT					IPC_DECL_FUNCTION(3)

/* UsbControllerType
 * Describes the possible types of usb controllers */
typedef enum _UsbControllerType {
	UsbOHCI,
	UsbUHCI,
	UsbEHCI,
	UsbXHCI
} UsbControllerType_t;

/* UsbSpeed 
 * Describes the possible speeds for usb devices */
typedef enum _UsbSpeed {
	LowSpeed,		// 1.0 / 1.1
	FullSpeed,		// 1.0 / 1.1 / 2.0 (HID)
	HighSpeed,		// 2.0
	SuperSpeed		// 3.0
} UsbSpeed_t;

/* UsbHcPortDescriptor 
 * Describes the current port information */
PACKED_TYPESTRUCT(UsbHcPortDescriptor, {
	UsbSpeed_t							Speed;
	int									Enabled;
	int									Connected;
});

/* UsbHcEndpointDescriptor 
 * Describes a generic endpoint for an usb device */
PACKED_TYPESTRUCT(UsbHcEndpointDescriptor, {
	UsbEndpointType_t 					Type;
	UsbEndpointSynchronization_t		Synchronization;
	size_t 								Address;
	size_t 								Direction;
	size_t 								MaxPacketSize;
	size_t 								Bandwidth;
	size_t 								Interval;
});

/* Bit-fields and definitions for field UsbHcEndpointDescriptor::Direction
 * Defined below */
#define USB_ENDPOINT_IN					0x0
#define USB_ENDPOINT_OUT				0x1
#define USB_ENDPOINT_BOTH				0x2

/* UsbControllerRegister
 * Registers a new controller with the given type and setup */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbControllerRegister(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbControllerType_t Type,
	_In_ size_t Ports);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbControllerRegister(
	_In_ UUId_t Device,
	_In_ UsbControllerType_t Type,
	_In_ size_t Ports)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize RPC
	RPCInitialize(&Request, __USBMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __USBMANAGER_REGISTERCONTROLLER);

	// Setup arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Type, sizeof(UsbControllerType_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&Ports, sizeof(size_t));

	// Send event, no response
	return RPCEvent(&Request, __USBMANAGER_TARGET);
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
	_In_ UUId_t Driver,
	_In_ UUId_t Device);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbControllerUnregister(
	_In_ UUId_t Device)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize RPC
	RPCInitialize(&Request, __USBMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __USBMANAGER_UNREGISTERCONTROLLER);

	// Setup arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));

	// Send event, no response
	return RPCEvent(&Request, __USBMANAGER_TARGET);
}
#endif

/* UsbEventPort 
 * Fired by a usbhost controller driver whenever there is a change
 * in port-status. The port-status is then queried automatically by
 * the usbmanager. */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbEventPort(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Index);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbEventPort(
	_In_ UUId_t Device,
	_In_ int Index)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize RPC
	RPCInitialize(&Request, __USBMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __USBMANAGER_PORTEVENT);

	// Setup arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Index, sizeof(int));

	// Send event, no response
	return RPCEvent(&Request, __USBMANAGER_TARGET);
}
#endif

/* UsbQueryControllers 
 * Queries the available usb controllers and their status in the system
 * The given array must be of size USB_MAX_CONTROLLERS. */

/* UsbQueryPorts 
 * Queries the available ports and their status on the given usb controller
 * the given array must be of size USB_MAX_PORTS. */

/* UsbQueryPipes 
 * Queries the available interfaces and endpoints on a given
 * port and controller. Querying with NULL pointers returns the count
 * otherwise fills the array given with information */

 /* UsbQueryDescriptor
  * Queries a common usb-descriptor from the given usb port and 
  * usb controller. The given array is filled with the descriptor information */
 
#endif //!_USB_INTERFACE_H_
