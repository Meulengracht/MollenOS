/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include "manager.h"
#include <usb/usb.h>

#include <sys_usb_service_server.h>

extern gracht_server_t* __crt_get_service_server(void);

void GetServiceAddress(IPCAddress_t* address)
{
    address->Type = IPC_ADDRESS_PATH;
    address->Data.Path = SERVICE_USB_PATH;
}

void ServiceInitialize(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &sys_usb_server_protocol);

    // Initialize all subsystems
    if (UsbCoreInitialize() != OsOK) {
        exit(-1);
    }
}


// for some reason these are pulled in by libddk and don't really care to fix it,
// because it happens due to my lazyness in libddk
void ctt_usbhub_event_port_status_invocation(gracht_client_t* client, const uuid_t id, const oserr_t result, const size_t bytesTransferred) {

}
void sys_device_event_protocol_device_invocation(gracht_client_t* client, const uuid_t deviceId, const uuid_t driverId, const uint8_t protocolId) {

}
void sys_device_event_device_update_invocation(gracht_client_t* client, const uuid_t deviceId, const uint8_t connected) {

}
void ctt_usbhost_event_transfer_status_invocation(gracht_client_t* client, const uuid_t transferId, const UsbTransferStatus_t status, const size_t dataIndex) {

}
