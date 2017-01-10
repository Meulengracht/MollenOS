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

#ifndef _MCORE_DEVICE_H_
#define _MCORE_DEVICE_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>
#include <os/ipc/ipc.h>
#include <os/driver/io.h>

/* The export macro, and is only set by the
 * the actual implementation of the devicemanager */
#ifdef __DEVICEMANAGER_EXPORT
#define __DEVAPI __CRT_EXTERN
#else
#define __DEVAPI static __CRT_INLINE
#endif

/* These definitions are in-place to allow a custom
 * setting of the device-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __DEVICEMANAGER_INTERFACE_VERSION	1
#define __DEVICEMANAGER_TARGET				0x8000

#define __DEVICEMANAGER_NAMEBUFFER_LENGTH	128
#define __DEVICEMANAGER_MAX_IRQS			8
#define __DEVICEMANAGER_MAX_IOSPACES		6
#define __DEVICEMANAGER_IOSPACE_END			-1

/* These are the different IPC functions supported
 * by the devicemanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __DEVICEMANAGER_REGISTERDEVICE		IPC_DECL_FUNCTION(0)
#define __DEVICEMANAGER_UNREGISTERDEVICE	IPC_DECL_FUNCTION(1)
#define __DEVICEMANAGER_QUERYDEVICE			IPC_DECL_FUNCTION(2)
#define __DEVICEMANAGER_IOCTLDEVICE			IPC_DECL_FUNCTION(3)

/* This is the base device structure definition
 * and is passed on to all drivers on their initialization
 * to give them an overview and description of their device 
 * and functions to read/write directly to the device */
typedef struct _MCoreDevice
{
	/* Device Identifier
	 * This is used when communicating with the
	 * devicemanager server */
	DevId_t						Id;

	/* Device Name
	 * Limited name buffer set by config 
	 * Null terminated utf8 data */
	char						Name[__DEVICEMANAGER_NAMEBUFFER_LENGTH];

	/* Device Information
	 * This is used both by the devicemanager
	 * and by the driver to match */
	DevInfo_t					VendorId;
	DevInfo_t					DeviceId;
	DevInfo_t					Class;
	DevInfo_t					Subclass;

	/* Device Irq Description
	 * This information descripes the type of
	 * irq, and the available irq lines when registering
	 * the device for interrupts */
	int							IrqLine;
	int							IrqPin;
	int							IrqAvailable[__DEVICEMANAGER_MAX_IRQS];

	/* Device I/O Spaces 
	 * These are the id's of the IO-spaces that
	 * belong to this device. */
	DeviceIoSpace_t				IoSpaces[__DEVICEMANAGER_MAX_IOSPACES];

	/* Device Bus Information 
	 * This describes the location on
	 * the bus, and these informations
	 * can be used to control the bus-device */
	DevInfo_t					Segment;
	DevInfo_t					Bus;
	DevInfo_t					Device;
	DevInfo_t					Function;

} MCoreDevice_t;

/* Device Registering
 * Allows registering of a new device in the
 * device-manager, and automatically queries
 * for a driver for the new device */
#ifdef __DEVICEMANAGER_EXPORT
__DEVAPI DevId_t RegisterDevice(MCoreDevice_t *Device);
#else
__DEVAPI DevId_t RegisterDevice(MCoreDevice_t *Device)
{
	/* Variables */
	MRemoteCall_t Request;
	DevId_t Result;
	RPCInitialize(&Request, PIPE_DEFAULT, __DEVICEMANAGER_REGISTERDEVICE);
	RPCSetArgument(&Request, 0, (const void*)Device, sizeof(MCoreDevice_t));
	RPCSetResult(&Request, (const void*)&Result, sizeof(DevId_t));
	RPCEvaluate(&Request, __DEVICEMANAGER_TARGET);
	return Result;
}
#endif

/* Device Initialization
 * Initializes the device for use and enables
 * any irq(s) that is associated for the device
 * the requesting driver must handle irq events */
_MOS_API int DeviceInitialize(MCoreDevice_t*);

/* Device Shutdown
 * Disables the device and unregisteres any irq
 * that might have been previously registered
 * and unloads any children devices */
_MOS_API int DeviceShutdown(MCoreDevice_t*);

/* Device Query
 * Queries the given device for information, see
 * the different query-types available above */
_MOS_API int DeviceQuery(MCoreDevice_t*);

/* Device Control
 * Control the given device by making modification
 * the to the bus settings or irq status */
_MOS_API int DeviceControl(MCoreDevice_t*);

#endif //!_MCORE_DEVICE_H_
