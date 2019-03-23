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

#include <ddk/contracts/usbhost.h>
#include <ddk/services/usb.h>
#include <ddk/bufferpool.h>
#include <os/osdefs.h>
#include <ds/collection.h>

// Forward declarations
typedef struct _UsbHub UsbHub_t;

/* UsbInterfaceVersion_t 
 * */
typedef struct _UsbInterfaceVersion {
    UsbHcInterfaceVersion_t     Base;
    int                         Exists;
    UsbHcEndpointDescriptor_t   Endpoints[USB_MAX_ENDPOINTS];
} UsbInterfaceVersion_t;

/* UsbInterface_t 
 * */
typedef struct _UsbInterface {
    UsbHcInterface_t            Base;
    int                         Exists;
    UUId_t                      DeviceId;
    UsbInterfaceVersion_t       Versions[USB_MAX_VERSIONS];
} UsbInterface_t;

/* UsbDevice_t 
 * */
typedef struct _UsbDevice {
    UsbHcDevice_t               Base;
    void*                       Descriptors;
    size_t                      DescriptorsBufferLength;
    
    // Buffers
    UsbInterface_t              Interfaces[USB_MAX_INTERFACES];
    UsbHcEndpointDescriptor_t   ControlEndpoint;
    UsbHub_t*                   Hub;
} UsbDevice_t;

/* UsbPort_t
 * */
typedef struct _UsbPort {
    uint8_t                     Address;
    UsbSpeed_t                  Speed;
    int                         Enabled;
    int                         Connected;
    UsbDevice_t*                Device;
} UsbPort_t;

/* UsbHub_t 
 * */
typedef struct _UsbHub {
    uint8_t                     Address;
    UsbPort_t*                  Ports[USB_MAX_PORTS];
} UsbHub_t;

/* UsbController_t
 * */
typedef struct _UsbController {
    MCoreDevice_t               Device;
    UUId_t                      DriverId;
    UsbControllerType_t         Type;
    size_t                      PortCount;

    // Address Map 
    // 4 x 32 bits = 128 possible addresses
    // which match the max in usb-spec
    uint32_t                    AddressMap[4];
    UsbHub_t                    RootHub;
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

/* UsbCoreControllerRegister
 * Registers a new controller with the given type and setup */
__EXTERN
OsStatus_t
UsbCoreControllerRegister(
    _In_ UUId_t                 DriverId,
    _In_ MCoreDevice_t*         Device,
    _In_ UsbControllerType_t    Type,
    _In_ size_t                 RootPorts);

/* UsbCoreControllerUnregister
 * Unregisters the given usb-controller from the manager and
 * unregisters any devices registered by the controller */
__EXTERN
OsStatus_t
UsbCoreControllerUnregister(
    _In_ UUId_t                 DriverId,
    _In_ UUId_t                 DeviceId);

/* UsbCoreEventPort 
 * Fired by a usbhost controller driver whenever there is a change
 * in port-status. The port-status is then queried automatically by
 * the usbmanager. */
__EXTERN
OsStatus_t
UsbCoreEventPort(
    _In_ UUId_t                 DriverId,
    _In_ UUId_t                 DeviceId,
    _In_ uint8_t                HubAddress,
    _In_ uint8_t                PortAddress);

/* UsbCoreGetControllerCount
 * Retrieves the number of registered controllers. */
__EXTERN
int
UsbCoreGetControllerCount(void);

/* UsbCoreGetController 
 * Looks up the controller that matches the device-identifier */
__EXTERN
UsbController_t*
UsbCoreGetController(
    _In_ UUId_t                 DeviceId);

/* UsbCoreGetControllerIndex
 * Looks up the controller that matches the list-index */
__EXTERN
UsbController_t*
UsbCoreGetControllerIndex(
    _In_ int                    Index);

#endif //!__USBMANAGER_H__
