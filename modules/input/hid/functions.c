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
 * Human Input Device Driver (Generic)
 */

#define __TRACE

#include <usb/usb.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <stdlib.h>
#include "hid.h"

static OsStatus_t __FillHidDescriptor(
    _In_ HidDevice_t*        hidDevice,
    _In_ UsbHidDescriptor_t* hidDescriptor)
{
    UsbTransferStatus_t status;

    TRACE("__FillHidDescriptor(hidDevice=0x%" PRIxIN ", hidDescriptor=0x%" PRIxIN ")",
          hidDevice, hidDescriptor);

    status = UsbExecutePacket(&hidDevice->Base.DeviceContext,
                              USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_IN,
                              USBPACKET_TYPE_GET_DESC, 0, DESCRIPTOR_TYPE_HID,
                              (uint16_t)hidDevice->InterfaceId,
                              sizeof(UsbHidDescriptor_t), (void*)hidDescriptor);

    if (status != TransferFinished) {
        ERROR("__FillHidDescriptor failed with code %u", status);
        return OsError;
    }
    else {
        return OsOK;
    }
}

static OsStatus_t __FillReportDescriptor(
    _In_ HidDevice_t* hidDevice,
    _In_ uint8_t      reportType,
    _In_ uint8_t      reportLength,
    _In_ uint8_t*     reportBuffer)
{
    UsbTransferStatus_t status;

    TRACE("__FillReportDescriptor(hidDevice=0x%" PRIxIN ", reportType=%u, reportLength=%u)",
          hidDevice, reportType, reportLength);

    status = UsbExecutePacket(&hidDevice->Base.DeviceContext,
                              USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_IN,
                              USBPACKET_TYPE_GET_DESC, 0, reportType,
                              (uint16_t)hidDevice->InterfaceId,
                              reportLength, (void*)reportBuffer);

    if (status != TransferFinished) {
        ERROR("__FillReportDescriptor failed with code %u", status);
        return OsError;
    }
    else {
        return OsOK;
    }
}

OsStatus_t
HidGetProtocol(
        _In_ HidDevice_t* hidDevice,
        _In_ uint8_t*     protocol)
{
    TRACE("HidSetProtocol(hidDevice=0x%" PRIxIN ", protocol=%i)", hidDevice, protocol);
    if (UsbExecutePacket(&hidDevice->Base.DeviceContext,
                         USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_IN,
                         HID_GET_PROTOCOL, 0, 0,
                         (uint16_t)hidDevice->InterfaceId, 1, protocol) != TransferFinished) {
        return OsError;
    }
    else {
        return OsOK;
    }
}

OsStatus_t
HidSetProtocol(
    _In_ HidDevice_t* hidDevice,
    _In_ uint8_t      protocol)
{
    TRACE("HidSetProtocol(hidDevice=0x%" PRIxIN ", protocol=%i)", hidDevice, protocol);
    if (UsbExecutePacket(&hidDevice->Base.DeviceContext,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS,
                HID_SET_PROTOCOL, protocol & 0xFF, 0,
                (uint16_t)hidDevice->InterfaceId, 0, NULL) != TransferFinished) {
        return OsError;
    }
    else {
        return OsOK;
    }
}

OsStatus_t
HidSetIdle(
    _In_ HidDevice_t* hidDevice,
    _In_ uint8_t      reportId,
    _In_ uint8_t      duration)
{
    TRACE("HidSetIdle(hidDevice=0x%" PRIxIN ", reportId=%i, duration=%i)",
          hidDevice, reportId, duration);

    // This request may stall, which means it's unsupported
    if (UsbExecutePacket(&hidDevice->Base.DeviceContext,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS,
                         HID_SET_IDLE, reportId, duration,
                         (uint16_t)hidDevice->InterfaceId, 0, NULL) == TransferFinished) {
        return OsOK;
    }
    else {
        return OsNotSupported;
    }
}

OsStatus_t
HidSetupGeneric(
    _In_ HidDevice_t* hidDevice)
{
    UsbHidDescriptor_t hidDescriptor;
    uint8_t*           reportDescriptor = NULL;
    size_t             reportLength;
    OsStatus_t         osStatus;
    TRACE("HidSetupGeneric(hidDevice=0x%" PRIxIN ")", hidDevice);

    osStatus = __FillHidDescriptor(hidDevice, &hidDescriptor);
    if (osStatus != OsOK) {
        ERROR("HidSetupGeneric failed to retrieve the default hid descriptor");
        return osStatus;
    }

    // Switch to report protocol
    if (hidDevice->CurrentProtocol == HID_DEVICE_PROTOCOL_BOOT) {
        uint8_t currentProtocol;
        osStatus = HidGetProtocol(hidDevice, &currentProtocol);
        if (osStatus != OsOK) {
            ERROR("HidSetupGeneric failed to get the current hid device protocol");
            return osStatus;
        }

        if (currentProtocol != HID_DEVICE_PROTOCOL_REPORT) {
            osStatus = HidSetProtocol(hidDevice, HID_DEVICE_PROTOCOL_REPORT);
            if (osStatus != OsOK) {
                ERROR("HidSetupGeneric failed to set the hid device into report protocol");
                return osStatus;
            }
        }
        hidDevice->CurrentProtocol = HID_DEVICE_PROTOCOL_REPORT;
    }

    // Put the device into idle-state
    // We might have to set Duration to 500 ms for keyboards, but has to be tested
    // time is calculated in 4ms resolution, so 500ms = Duration = 125
    osStatus = HidSetIdle(hidDevice, 0, 0);
    if (osStatus != OsOK) {
        WARNING("HidSetupGeneric SetIdle failed, it might be unsupported by the HID");
    }

    reportDescriptor = (uint8_t*)malloc(hidDescriptor.ClassDescriptorLength);
    if (!reportDescriptor) {
        return OsOutOfMemory;
    }

    osStatus = __FillReportDescriptor(hidDevice, hidDescriptor.ClassDescriptorType,
                                      hidDescriptor.ClassDescriptorLength, reportDescriptor);
    if (osStatus != OsOK) {
        ERROR("HidSetupGeneric failed to retrieve the report descriptor");
        free(reportDescriptor);
        return osStatus;
    }

    reportLength = HidParseReportDescriptor(hidDevice, reportDescriptor, hidDescriptor.ClassDescriptorLength);
    free(reportDescriptor);

    hidDevice->ReportLength = reportLength;
    return OsOK;
}
