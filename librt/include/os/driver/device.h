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
 * MollenOS MCore - Device Definitions & Structures
 * - This header describes the base device-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _DEVICE_INTERFACE_H_
#define _DEVICE_INTERFACE_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>
#include <os/ipc/ipc.h>
#include <os/driver/io.h>
#include <os/driver/service.h>
#include <os/driver/interrupt.h>
#include <stddef.h>

/* The export macro, and is only set by the
 * the actual implementation of the devicemanager */
#ifdef __DEVICEMANAGER_IMPL
#define __DEVAPI __EXTERN
#else
#define __DEVAPI static __CRT_INLINE
#endif

/* These definitions are in-place to allow a custom
 * setting of the device-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __DEVICEMANAGER_INTERFACE_VERSION		1

#define __DEVICEMANAGER_NAMEBUFFER_LENGTH		128
#define __DEVICEMANAGER_MAX_IOSPACES			6
#define __DEVICEMANAGER_IOSPACE_END				-1

/* MCoreDevice ACPI Conform flags
 * This is essentially some bonus information that is
 * needed when registering interrupts */
#define __DEVICEMANAGER_ACPICONFORM_PRESENT		0x00000001
#define __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE	0x00000002
#define __DEVICEMANAGER_ACPICONFORM_POLARITY	0x00000004
#define __DEVICEMANAGER_ACPICONFORM_SHAREABLE	0x00000008
#define __DEVICEMANAGER_ACPICONFORM_FIXED		0x00000010

/* MCoreDevice Register Flags
 * Flags related to registering of new devices */
#define __DEVICEMANAGER_REGISTER_LOADDRIVER		0x00000001

/* MCoreDevice IoCtrl Flags
 * Flags related to registering of new devices */
#define __DEVICEMANAGER_IOCTL_BUS				0x00000000
#define __DEVICEMANAGER_IOCTL_EXT				0x00000001

// Ioctl-Bus Specific Flags
#define __DEVICEMANAGER_IOCTL_ENABLE			0x00000001
#define __DEVICEMANAGER_IOCTL_IO_ENABLE			0x00000002
#define __DEVICEMANAGER_IOCTL_MMIO_ENABLE		(0x00000002 | 0x00000004)
#define __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE	0x00000008
#define __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE	0x00000010  // Fast Back-To-Back

// Ioctl-Ext Specific Flags
#define __DEVICEMANAGER_IOCTL_EXT_WRITE			0x00000000
#define __DEVICEMANAGER_IOCTL_EXT_READ			0x80000000

/* These are the different IPC functions supported
 * by the devicemanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __DEVICEMANAGER_REGISTERDEVICE			IPC_DECL_FUNCTION(0)
#define __DEVICEMANAGER_UNREGISTERDEVICE		IPC_DECL_FUNCTION(1)
#define __DEVICEMANAGER_QUERYDEVICE				IPC_DECL_FUNCTION(2)
#define __DEVICEMANAGER_IOCTLDEVICE				IPC_DECL_FUNCTION(3)

#define __DEVICEMANAGER_REGISTERCONTRACT		IPC_DECL_FUNCTION(4)
#define __DEVICEMANAGER_UNREGISTERCONTRACT		IPC_DECL_FUNCTION(5)
#define __DEVICEMANAGER_QUERYCONTRACT			IPC_DECL_FUNCTION(6)

/* This is the base device structure definition
 * and is passed on to all drivers on their initialization
 * to give them an overview and description of their device 
 * and functions to read/write directly to the device */
PACKED_TYPESTRUCT(MCoreDevice, {
	UUId_t						Id;
	char						Name[__DEVICEMANAGER_NAMEBUFFER_LENGTH];

	// Device Information
	// This is used both by the devicemanager
	// and by the driver to match
	DevInfo_t					VendorId;
	DevInfo_t					DeviceId;
	DevInfo_t					Class;
	DevInfo_t					Subclass;

	// Interrupt and I/O Space information
	MCoreInterrupt_t			Interrupt;
	DeviceIoSpace_t				IoSpaces[__DEVICEMANAGER_MAX_IOSPACES];

	// Device Bus Information 
	// This describes the location on
	// the bus, and these informations
	// can be used to control the bus-device
	DevInfo_t					Segment;
	DevInfo_t					Bus;
	DevInfo_t					Slot;
	DevInfo_t					Function;

});

/* Device Registering
 * Allows registering of a new device in the
 * device-manager, and automatically queries
 * for a driver for the new device */
#ifdef __DEVICEMANAGER_IMPL
__DEVAPI
OsStatus_t
SERVICEABI
RegisterDevice(
	_In_ UUId_t Parent,
	_In_ MCoreDevice_t *Device, 
	_In_ __CONST char *Name,
	_In_ Flags_t Flags,
	_Out_ UUId_t *Id);
#else
__DEVAPI
UUId_t
SERVICEABI
RegisterDevice(
	_In_ UUId_t Parent,
	_In_ MCoreDevice_t *Device, 
	_In_ Flags_t Flags)
{
	// Variables
	MRemoteCall_t Request;
	UUId_t Result = UUID_INVALID;

	// Initialize RPC
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION, 
		PIPE_RPCOUT, __DEVICEMANAGER_REGISTERDEVICE);
	RPCSetArgument(&Request, 0, (__CONST void*)&Parent, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)Device, sizeof(MCoreDevice_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&Flags, sizeof(Flags_t));
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(UUId_t));
	
	// Execute RPC
	RPCExecute(&Request, __DEVICEMANAGER_TARGET);
	return Result;
}
#endif

/* Device I/O Control
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device */
#ifdef __DEVICEMANAGER_IMPL
__DEVAPI
OsStatus_t
SERVICEABI
IoctlDevice(
	_In_ MCoreDevice_t *Device,
	_In_ Flags_t Flags);
#else
__DEVAPI
OsStatus_t
SERVICEABI
IoctlDevice(
	_In_ UUId_t Device,
	_In_ Flags_t Command,
	_In_ Flags_t Flags)
{
	// Variables
	MRemoteCall_t Request;
	OsStatus_t Result = OsSuccess;

	// Initialize RPC
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION, 
		PIPE_RPCOUT, __DEVICEMANAGER_IOCTLDEVICE);
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Command, sizeof(Flags_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&Flags, sizeof(Flags_t));
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(OsStatus_t));
	
	// Execute RPC
	RPCExecute(&Request, __DEVICEMANAGER_TARGET);
	return Result;
}
#endif

/* Device I/O Control (Extended)
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device.
 * <Direction> = 0 (Read), 1 (Write) */
#ifdef __DEVICEMANAGER_IMPL
__DEVAPI
Flags_t
SERVICEABI
IoctlDeviceEx(
	_In_ MCoreDevice_t *Device,
	_In_ Flags_t Parameters,
	_In_ Flags_t Register,
	_In_ Flags_t Value,
	_In_ size_t Width);
#else
__DEVAPI
OsStatus_t
SERVICEABI
IoctlDeviceEx(
	_In_ UUId_t Device,
	_In_ int Direction,
	_In_ Flags_t Register,
	_InOut_ Flags_t *Value,
	_In_ size_t Width)
{
	// Variables
	MRemoteCall_t Request;
	Flags_t Result = 0;
	Flags_t Select = 0;

	// Build selection
	Select = __DEVICEMANAGER_IOCTL_EXT;
	if (Direction == 0) {
		Select |= __DEVICEMANAGER_IOCTL_EXT_READ;
	}

	// Initialize RPC
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION, 
		PIPE_RPCOUT, __DEVICEMANAGER_IOCTLDEVICE);
	RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Select, sizeof(Flags_t));
	RPCSetArgument(&Request, 2, (__CONST void*)&Register, sizeof(Flags_t));
	RPCSetArgument(&Request, 3, (__CONST void*)Value, sizeof(Flags_t));
	RPCSetArgument(&Request, 4, (__CONST void*)&Width, sizeof(size_t));
	RPCSetResult(&Request, (__CONST void*)&Result, sizeof(Flags_t));
	
	// Execute RPC
	RPCExecute(&Request, __DEVICEMANAGER_TARGET);

	// Handle return
	if (Direction == 0 && Value != NULL) {
		*Value = Result;
	}
	else {
		return (OsStatus_t)Result;
	}

	// Read, discard value
	return OsSuccess;
}
#endif

/* InstallDriver 
 * Tries to find a suitable driver for the given device
 * by searching storage-medias for the vendorid/deviceid 
 * combination or the class/subclass combination if specific
 * is not found */
MOSAPI
OsStatus_t
MOSABI
InstallDriver(
	_In_ MCoreDevice_t *Device,
	_In_ size_t Length);

#endif //!_DEVICE_INTERFACE_H_
