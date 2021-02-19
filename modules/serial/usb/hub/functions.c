/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * Human Input Device Driver (Generic)
 */

#define __TRACE

#include <usb/usb.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "hub.h"

OsStatus_t
HubGetStatus(
        _In_ HubDevice_t* hubDevice,
        _In_ HubStatus_t* status)
{
    TRACE("HubGetStatus(hubDevice=0x%" PRIxIN ")", hubDevice);
    if (UsbExecutePacket(&hubDevice->Base.DeviceContext,
                         USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS, USBPACKET_TYPE_GET_STATUS,
                         0, 0,
                         0, 4, status) != TransferFinished) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t
HubGetPortStatus(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex,
        _In_ PortStatus_t* status)
{
    TRACE("HubGetPortStatus(hubDevice=0x%" PRIxIN ")", hubDevice);
    if (UsbExecutePacket(&hubDevice->Base.DeviceContext,
                         USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_OTHER,
                         USBPACKET_TYPE_GET_STATUS, 0, 0,
                         portIndex, 4, status) != TransferFinished) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

