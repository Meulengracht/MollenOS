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
#include <os/ipc/rpc.h>
#include <os/osdefs.h>

/* These definitions are in-place to allow a custom
 * setting of the device-manager, these are set to values
 * where in theory it should never be needed to have more */
#define DEVICE_INVALID		0
#define MAX_IRQS			8
#define MAX_IOSPACES		6
#define IOSPACE_END			-1

/* The export/usage macro definition
 * since only the actually systems export
 * and everything else imports/uses */
#ifdef __DEVICEMANAGER_EXPORT
#define __DEV_API __declspec(dllexport)
#else
#define __DEV_API static __CRT_INLINE
#endif

/* Definitions for the device-api, these are 
 * used only by this interface */
#define __DEVICEMANAGER_LIBRARY			"devicemanager.dll"
#define __DEVICEMANAGER_VERSION			1

/* Guard against CPP code */
#ifdef __cplusplus
extern "C" {
#endif

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

/* The register device RPC
 * Use this to register a new device with the device-manager
 * the returned value will be the id of the new device */
__DEV_API
#ifdef __DEVICEMANAGER_EXPORT
DECLRPC(RegisterDevice(MCoreDevice_t *Device));
#else
DevId_t RegisterDevice(MCoreDevice_t *Device)
{
	/* Variables */
	MCoreRPC_t RpcPackage;
	DevId_t DeviceId = 0;
	int ErrorCode = 0;
	void *Result = NULL;

	/* Initialize a new RPC structure 
	 * with the correct function name, and function
	 * parameter package */
	RPCInitialize(&RpcPackage, __DEVICEMANAGER_LIBRARY,
		"RegisterDevice", __DEVICEMANAGER_VERSION);
	RPCSetArgument(&RpcPackage, 0, 
		(const void*)Device, sizeof(MCoreDevice_t));
	Result = RPCEvaluate(&RpcPackage, &ErrorCode);

	/* Sanitize the result */
	if (ErrorCode == 0
		&& Result != NULL) {
		return *((DevId_t*)Result);
	}
	else {
		return DEVICE_INVALID;
	}
}
#endif

/* End of the cpp guard */
#ifdef __cplusplus
}
#endif

#endif //!_MCORE_DEVICE_H_
