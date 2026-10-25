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
#include <stdlib.h>
#include "hid.h"

static oserr_t
__FindHidDescriptor(
    _In_  const uint8_t*      configuration,
    _In_  size_t              capacity,
    _In_  const HidDevice_t*  hidDevice,
    _Out_ UsbHidDescriptor_t* hidDescriptor)
{
    const uint8_t*  descriptor;
    size_t          length;
    size_t          totalLength;
    bool            selected = false;

    if (capacity < sizeof(usb_config_descriptor_t)) {
        return OS_EINVALPARAMS;
    }

    totalLength = configuration[2] | ((size_t)configuration[3] << 8);
    if (configuration[1] != USB_DESCRIPTOR_CONFIG ||
        configuration[0] < sizeof(usb_config_descriptor_t)) {
        return OS_EINVALPARAMS;
    }
    
    if (totalLength > capacity || totalLength < configuration[0]) {
        return OS_EINVALPARAMS;
    }

    for (size_t offset = configuration[0]; offset < totalLength; offset += length) {
        if (totalLength - offset < 2) {
            return OS_EINVALPARAMS;
        }

        descriptor = configuration + offset;
        length = descriptor[0];
        if (length < 2 || length > totalLength - offset) {
            return OS_EINVALPARAMS;
        }
        
        if (descriptor[1] == USB_DESCRIPTOR_INTERFACE) {
            if (length < sizeof(usb_interface_descriptor_t)) {
                return OS_EINVALPARAMS;
            }
            
            selected = descriptor[2] == hidDevice->InterfaceId &&
                    descriptor[3] == hidDevice->InterfaceSetting &&
                    descriptor[5] == USB_CLASS_HID;
        } else if (selected && descriptor[1] == DESCRIPTOR_TYPE_HID) {
            if (length < sizeof(UsbHidDescriptor_t) ||
                descriptor[5] == 0 || descriptor[5] > (length - 6) / 3) {
                return OS_EINVALPARAMS;
            }
            
            for (unsigned int i = 0; i < descriptor[5]; i++) {
                if (descriptor[6 + i * 3] != DESCRIPTOR_TYPE_REPORT) {
                    continue;
                }
                memcpy(hidDescriptor, descriptor, 6);
                memcpy(&hidDescriptor->ClassDescriptorType, descriptor + 6 + i * 3, 3);
                return hidDescriptor->ClassDescriptorLength != 0 ? OS_EOK : OS_EINVALPARAMS;
            }
            return OS_ENOENT;
        }
    }
    return OS_ENOENT;
}

static oserr_t
__FillHidDescriptor(
    _In_  HidDevice_t*        hidDevice,
    _Out_ UsbHidDescriptor_t* hidDescriptor)
{
    enum USBTransferCode status;
    size_t               length = hidDevice->Base->DeviceContext.configuration_length;
    uint8_t*             configuration;
    oserr_t              oserr;

    if (length < sizeof(usb_config_descriptor_t)) {
        return OS_EINVALPARAMS;
    }

    configuration = calloc(1, length);
    if (configuration == NULL) {
        return OS_EOOM;
    }

    // The HID descriptor belongs to the selected interface in the configuration.
    // Some devices provide it only there and stall a standalone HID request.
    // libusb's parsed configuration currently retains only interfaces/endpoints.
    status = UsbExecutePacket(
        &hidDevice->Base->DeviceContext,
        USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC,
        0,
        USB_DESCRIPTOR_CONFIG,
        0,
        (uint16_t)length,
        configuration
    );
    if (status != USBTRANSFERCODE_SUCCESS) {
        free(configuration);
        return OS_EUNKNOWN;
    }

    oserr = __FindHidDescriptor(configuration, length, hidDevice, hidDescriptor);
    free(configuration);
    return oserr;
}

static oserr_t __FillReportDescriptor(
    _In_ HidDevice_t* hidDevice,
    _In_ uint8_t      reportType,
    _In_ uint16_t     reportLength,
    _In_ uint8_t*     reportBuffer)
{
    enum USBTransferCode status;

    TRACE("__FillReportDescriptor(hidDevice=0x%" PRIxIN ", reportType=%u, reportLength=%u)",
          hidDevice, reportType, reportLength);

    status = UsbExecutePacket(&hidDevice->Base->DeviceContext,
                              USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_IN,
                              USBPACKET_TYPE_GET_DESC, 0, reportType,
                              (uint16_t)hidDevice->InterfaceId,
                              reportLength, (void*)reportBuffer);

    if (status != USBTRANSFERCODE_SUCCESS) {
        ERROR("__FillReportDescriptor failed with code %u", status);
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
HidGetProtocol(
        _In_ HidDevice_t* hidDevice,
        _In_ uint8_t*     protocol)
{
    TRACE("HidSetProtocol(hidDevice=0x%" PRIxIN ", protocol=%i)", hidDevice, protocol);
    if (UsbExecutePacket(&hidDevice->Base->DeviceContext,
                         USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_IN,
                         HID_GET_PROTOCOL, 0, 0,
                         (uint16_t)hidDevice->InterfaceId, 1, protocol) != USBTRANSFERCODE_SUCCESS) {
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
HidSetProtocol(
    _In_ HidDevice_t* hidDevice,
    _In_ uint8_t      protocol)
{
    TRACE("HidSetProtocol(hidDevice=0x%" PRIxIN ", protocol=%i)", hidDevice, protocol);
    if (UsbExecutePacket(&hidDevice->Base->DeviceContext,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS,
                HID_SET_PROTOCOL, protocol & 0xFF, 0,
                (uint16_t)hidDevice->InterfaceId, 0, NULL) != USBTRANSFERCODE_SUCCESS) {
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
HidSetIdle(
    _In_ HidDevice_t* hidDevice,
    _In_ uint8_t      reportId,
    _In_ uint8_t      duration)
{
    TRACE("HidSetIdle(hidDevice=0x%" PRIxIN ", reportId=%i, duration=%i)",
          hidDevice, reportId, duration);

    // This request may stall, which means it's unsupported
    if (UsbExecutePacket(&hidDevice->Base->DeviceContext,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS,
                         HID_SET_IDLE, reportId, duration,
                         (uint16_t)hidDevice->InterfaceId, 0, NULL) == USBTRANSFERCODE_SUCCESS) {
        return OS_EOK;
    }
    else {
        return OS_ENOTSUPPORTED;
    }
}

oserr_t
HidSetupGeneric(
    _In_ HidDevice_t* hidDevice)
{
    UsbHidDescriptor_t hidDescriptor;
    uint8_t*           reportDescriptor = NULL;
    size_t             reportLength;
    oserr_t         osStatus;
    TRACE("HidSetupGeneric(hidDevice=0x%" PRIxIN ")", hidDevice);

    osStatus = __FillHidDescriptor(hidDevice, &hidDescriptor);
    if (osStatus != OS_EOK) {
        ERROR("HidSetupGeneric failed to retrieve the default hid descriptor");
        return osStatus;
    }

    if (hidDescriptor.Length < sizeof(UsbHidDescriptor_t) ||
        hidDescriptor.Type != DESCRIPTOR_TYPE_HID ||
        hidDescriptor.NumDescriptors == 0 ||
        hidDescriptor.ClassDescriptorType != DESCRIPTOR_TYPE_REPORT ||
        hidDescriptor.ClassDescriptorLength == 0) {
        ERROR("HidSetupGeneric invalid HID class descriptor");
        return OS_EINVALPARAMS;
    }

    // Switch to report protocol
    if (hidDevice->CurrentProtocol == HID_DEVICE_PROTOCOL_BOOT) {
        uint8_t currentProtocol;
        osStatus = HidGetProtocol(hidDevice, &currentProtocol);
        if (osStatus != OS_EOK) {
            ERROR("HidSetupGeneric failed to get the current hid device protocol");
            return osStatus;
        }

        if (currentProtocol != HID_DEVICE_PROTOCOL_REPORT) {
            osStatus = HidSetProtocol(hidDevice, HID_DEVICE_PROTOCOL_REPORT);
            if (osStatus != OS_EOK) {
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
    if (osStatus != OS_EOK) {
        WARNING("HidSetupGeneric SetIdle failed, it might be unsupported by the HID");
    }

    reportDescriptor = (uint8_t*)malloc(hidDescriptor.ClassDescriptorLength);
    if (!reportDescriptor) {
        return OS_EOOM;
    }

    osStatus = __FillReportDescriptor(hidDevice, hidDescriptor.ClassDescriptorType,
                                      hidDescriptor.ClassDescriptorLength, reportDescriptor);
    if (osStatus != OS_EOK) {
        ERROR("HidSetupGeneric failed to retrieve the report descriptor");
        free(reportDescriptor);
        return osStatus;
    }

    reportLength = HidParseReportDescriptor(hidDevice, reportDescriptor, hidDescriptor.ClassDescriptorLength);
    free(reportDescriptor);

    if (reportLength == 0 || reportLength > 0x400) {
        ERROR("HidSetupGeneric report length %" PRIuIN " exceeds the interrupt buffer", reportLength);
        return OS_EINVALPARAMS;
    }

    hidDevice->ReportLength = reportLength;
    return OS_EOK;
}
