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
 * MollenOS Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#ifndef __USBMANAGER_H__
#define __USBMANAGER_H__

/* Includes 
 * - System */
#include <os/driver/contracts/usbhost.h>
#include <os/driver/bufferpool.h>
#include <os/driver/usb.h>
#include <os/osdefs.h>
#include <ds/list.h>

/* UsbInterfaceVersion_t 
 * */
typedef struct _UsbInterfaceVersion {
	UsbHcInterfaceVersion_t		Base;
	int							Exists;
	UsbHcEndpointDescriptor_t	Endpoints[USB_MAX_ENDPOINTS];
} UsbInterfaceVersion_t;

/* UsbInterface_t 
 * */
typedef struct _UsbInterface {
	UsbHcInterface_t			Base;
	int							Exists;
	UsbInterfaceVersion_t		Versions[USB_MAX_VERSIONS];
} UsbInterface_t;

/* UsbDevice_t 
 * */
typedef struct _UsbDevice {
	UsbHcDevice_t				Base;
	void*						Descriptors;
	size_t						DescriptorsBufferLength;
	
	// Buffers
	UsbInterface_t				Interfaces[USB_MAX_INTERFACES];
	UsbHcEndpointDescriptor_t 	ControlEndpoint;
} UsbDevice_t;

/* UsbPort_t
 * */
typedef struct _UsbPort {
	int					 Index;
	UsbSpeed_t			 Speed;
	int					 Enabled;
	int					 Connected;
	UsbDevice_t			*Device;
} UsbPort_t;

/* UsbController_t
 * */
typedef struct _UsbController {
    UUId_t               Driver;
	UUId_t               Device;
	UsbControllerType_t  Type;
	size_t               PortCount;

	// Address Map 
	// 4 x 32 bits = 128 possible addresses
	// which match the max in usb-spec
	uint32_t             AddressMap[4];

    // Ports
	UsbPort_t           *Ports[USB_MAX_PORTS];
} UsbController_t;

/* UsbCoreInitialize
 * Initializes the usb-core stack driver. Allocates all neccessary resources
 * for managing usb controllers devices in the system. */
__EXTERN
OsStatus_t
UsbCoreInitialize(void);

/* UsbCoreDestroy
 * Cleans up and frees any resouces allocated by the usb-core stack */
__EXTERN
OsStatus_t
UsbCoreDestroy(void);

/* UsbCoreGetBufferPool
 * Retrieves the buffer-pool that can be used for internal
 * usb-transfers. Usefull for enumeration. */
 __EXTERN
 BufferPool_t*
 UsbCoreGetBufferPool(void);

/* UsbFunctionSetAddress
 * Set address of an usb device - the device structure is automatically updated. */
__EXTERN
UsbTransferStatus_t
UsbFunctionSetAddress(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port, 
	_In_ int Address);

/* UsbFunctionGetDeviceDescriptor
 * Queries the device descriptor of an usb device on a given port. The information
 * is automatically filled in, in the device structure */
__EXTERN
UsbTransferStatus_t
UsbFunctionGetDeviceDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port);

/* UsbFunctionGetConfigDescriptor
 * Queries the full configuration descriptor setup including all endpoints and interfaces.
 * This relies on the GetInitialConfigDescriptor. Also allocates all resources neccessary. */
__EXTERN
UsbTransferStatus_t
UsbFunctionGetConfigDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port);

/* UsbFunctionSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
__EXTERN
UsbTransferStatus_t
UsbFunctionSetConfiguration(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port,
	_In_ size_t Configuration);

/* UsbFunctionGetStringLanguages
 * Gets the device string language descriptors (Index 0). This automatically stores the available
 * languages in the device structure. */
__EXTERN
UsbTransferStatus_t
UsbFunctionGetStringLanguages(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port);

/* UsbFunctionGetStringDescriptor
 * Queries the usb device for a string with the given language and index. */
__EXTERN
UsbTransferStatus_t
UsbFunctionGetStringDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port, 
	_In_ size_t LanguageId, 
	_In_ size_t StringIndex, 
	_Out_ char *String);

/* UsbFunctionClearFeature
 * Indicates to an usb-device that we want to request a feature/state disabled. */
__EXTERN
UsbTransferStatus_t
UsbFunctionClearFeature(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
	_In_ uint16_t Feature);

/* UsbFunctionSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
__EXTERN
UsbTransferStatus_t
UsbFunctionSetFeature(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
	_In_ uint16_t Feature);

#endif //!__USBMANAGER_H__
