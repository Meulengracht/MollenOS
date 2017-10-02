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
 * MollenOS MCore - Contract Definitions & Structures (Usb-Host Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_USBHOST_INTERFACE_H_
#define _CONTRACT_USBHOST_INTERFACE_H_

/* Includes 
 * - System */
#include <os/osdefs.h>
#include <os/driver/driver.h>
#include <os/driver/usb.h>

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

/* Usb host controller query functions that must be implemented
 * by the usb host driver - those can then be used by this interface */
#define __USBHOST_QUEUETRANSFER		IPC_DECL_FUNCTION(0)
#define __USBHOST_QUEUEPERIODIC		IPC_DECL_FUNCTION(1)
#define __USBHOST_DEQUEUEPERIODIC	IPC_DECL_FUNCTION(2)
#define __USBHOST_RESETPORT			IPC_DECL_FUNCTION(3)
#define __USBHOST_QUERYPORT			IPC_DECL_FUNCTION(4)
#define __USBHOST_RESETENDPOINT     IPC_DECL_FUNCTION(5)

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

#endif //!_CONTRACT_USBHOST_INTERFACE_H_
