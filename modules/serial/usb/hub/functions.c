/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define __TRACE

#include <usb/usb.h>
#include <ddk/utils.h>
#include <threads.h>
#include "hub.h"

oserr_t
HubGetStatus(
        _In_ HubDevice_t* hubDevice,
        _In_ HubStatus_t* status)
{
    TRACE("HubGetStatus(hubDevice=0x%" PRIxIN ")", hubDevice);
    if (UsbExecutePacket(&hubDevice->Base->DeviceContext,
                         USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS,
                         USBPACKET_TYPE_GET_STATUS, 0, 0,
                         0, 4, status) != USBTRANSFERCODE_SUCCESS) {
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
HubGetPortStatus(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex,
        _In_ PortStatus_t* status)
{
    TRACE("HubGetPortStatus(hubDevice=0x%" PRIxIN ")", hubDevice);
    if (UsbExecutePacket(&hubDevice->Base->DeviceContext,
                         USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_OTHER,
                         USBPACKET_TYPE_GET_STATUS, 0, 0,
                         portIndex, 4, status) != USBTRANSFERCODE_SUCCESS) {
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
HubClearChange(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       change)
{
    enum USBTransferCode transferStatus;
    TRACE("HubClearChange(hubDevice=0x%" PRIxIN ", change=0x%x)", hubDevice, change);

    transferStatus = UsbExecutePacket(&hubDevice->Base->DeviceContext, USBPACKET_DIRECTION_CLASS,
                              USBPACKET_TYPE_CLR_FEATURE, change,
                              0, 0, 0, NULL);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        return OS_EDEVFAULT;
    }
    return OS_EOK;
}

oserr_t
HubPortClearChange(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex,
        _In_ uint8_t       change)
{
    enum USBTransferCode transferStatus;
    TRACE("HubPortClearChange(hubDevice=0x%" PRIxIN ", portIndex=%u, change=0x%x)",
          hubDevice, portIndex, change);

    transferStatus = UsbExecutePacket(&hubDevice->Base->DeviceContext,
                                      USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_OTHER,
                                      USBPACKET_TYPE_CLR_FEATURE, change,
                                      0, portIndex, 0, NULL);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        return OS_EDEVFAULT;
    }
    return OS_EOK;
}

oserr_t
HubPowerOnPort(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex)
{
    enum USBTransferCode transferStatus;
    TRACE("HubPowerOnPort(hubDevice=0x%" PRIxIN ", portIndex=%u)", hubDevice, portIndex);

    transferStatus = UsbSetFeature(&hubDevice->Base->DeviceContext,
                                   USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_OTHER,
                                   portIndex,
                                   HUB_FEATURE_PORT_POWER);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        return OS_EDEVFAULT;
    }
    return OS_EOK;
}

oserr_t
HubResetPort(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex)
{
    enum USBTransferCode transferStatus;
    PortStatus_t         portStatus;
    TRACE("HubResetPort(hubDevice=0x%" PRIxIN ", portIndex=%u)", hubDevice, portIndex);

    transferStatus = UsbSetFeature(&hubDevice->Base->DeviceContext,
                                   USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_OTHER,
                                   portIndex,
                                   HUB_FEATURE_PORT_RESET);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        return OS_EDEVFAULT;
    }

    for (int attempt = 0; attempt < 50; attempt++) {
        if (HubGetPortStatus(hubDevice, portIndex, &portStatus) != OS_EOK) {
            return OS_EDEVFAULT;
        }
        if (!(portStatus.Status & HUB_PORT_STATUS_RESET)) {
            if (portStatus.Change & HUB_PORT_CHANGE_RESET) {
                HubPortClearChange(hubDevice, portIndex, HUB_FEATURE_C_PORT_RESET);
            }
            return OS_EOK;
        }
        thrd_sleep(&(struct timespec) { .tv_nsec = 10 * NSEC_PER_MSEC }, NULL);
    }
    return OS_ETIMEOUT;
}
