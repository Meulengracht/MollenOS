/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
#include <os/ipc.h>

/* These definitions are in-place to allow a custom
 * setting of the device-manager, these are set to values
 * where in theory it should never be needed to have more */
#define MAX_IRQS			8
#define MAX_IOSPACES		6
#define IOSPACE_END			-1

/* This is the base device structure definition
 * and is passed on to all drivers on their initialization
 * to give them an overview and description of their device 
 * and functions to read/write directly to the device */
typedef struct _MCoreDevice
{
	/* Device Identifier
	 * This is used when communicating with the
	 * devicemanager server */
	DevId_t Id;

	/* Device Information
	 * This is used both by the devicemanager
	 * and by the driver to match */
	DevInfo_t VendorId;
	DevInfo_t DeviceId;
	DevInfo_t Class;
	DevInfo_t Subclass;

	/* Device Irq Description
	 * This information descripes the type of
	 * irq, and the available irq lines when registering
	 * the device for interrupts */
	int IrqLine;
	int IrqPin;
	int IrqAvailable[MAX_IRQS];

	/* Device I/O Spaces 
	 * These are the id's of the IO-spaces that
	 * belong to this device, use IoSpaceQuery
	 * to find out more information. */
	IoSpaceId_t IoSpaces[MAX_IOSPACES];

	/* Device Bus Information 
	 * This describes the location on
	 * the bus, and these informations
	 * can be used to control the bus-device */
	DevInfo_t Segment;
	DevInfo_t Bus;
	DevInfo_t Device;
	DevInfo_t Function;

} MCoreDevice_t;

/* Device Manager IPC Requests 
 * These are the IPC requests that
 * the devicemanager can handle */
typedef enum _MCoreDeviceRequestType
{
	IpcRegisterDevice,
	IpcUnregisterDevice,

	IpcQueryDevice,
	IpcControlDevice

} MCoreDeviceRequestType_t;

/* Device Manager IPC Base 
 * This describes the base message of
 * a request for the device-manager and
 * should be used for all communication 
 * from/to device manager */
typedef struct _MCoreDeviceRequest
{
	/* Base of an IPC */
	MEventMessageBase_t Base;

	/* Ipc Request Type */
	MCoreDeviceRequestType_t Type;

	/* The device in question for the
	 * ipc operation - only need the id */
	DevId_t DeviceId;

	/* Fields here are read only
	 * they are response fields that
	 * results of the ipc request */
	OsStatus_t Result;

} MCoreDeviceRequest_t;

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
