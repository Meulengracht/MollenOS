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

#include <ddk/ddkdefs.h>
#include <ddk/usb.h>

/* These definitions are in-place to allow a custom
 * setting of the device-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __USBHUB_INTERFACE_VERSION              1

/* Usb hub query functions that must be implemented
 * by the usb hub driver - those can then be used by this interface */
#define __USBHUB_PORTSTATUS                     IPC_DECL_FUNCTION(0)
#define __USBHUB_CLEAR_FEATURE                  IPC_DECL_FUNCTION(1)
#define __USBHUB_SET_FEATURE                    IPC_DECL_FUNCTION(2)

/* UsbHubGetPortStatus 
 * Fired by a hub driver whenever there is a change
 * in port-status. The port-status is then queried automatically by
 * the usbmanager. */
SERVICEAPI
OsStatus_t
SERVICEABI
UsbHubGetPortStatus(
    _In_ UUId_t     DeviceId,
    _In_ uint8_t    PortAddress)
{
    // Variables
    MRemoteCall_t Request;

    // Initialize RPC
    RPCInitialize(&Request, __USBMANAGER_TARGET, 
        __USBHUB_INTERFACE_VERSION, __USBMANAGER_PORTEVENT);

    // Setup arguments
    RPCSetArgument(&Request, 0, (const void*)&DeviceId, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)&HubAddress, sizeof(uint8_t));
    RPCSetArgument(&Request, 2, (const void*)&PortAddress, sizeof(uint8_t));
    return RPCEvent(&Request);
}

#endif //!_CONTRACT_USBHOST_INTERFACE_H_
