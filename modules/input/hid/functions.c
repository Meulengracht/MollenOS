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
 * MollenOS MCore - Human Input Device Driver (Generic)
 */

#include <ddk/utils.h>
#include <ddk/usb.h>
#include <stdlib.h>
#include "hid.h"

/* HidGetDescriptor
 * Retrieves the HID descriptor from the usb-device. */
OsStatus_t
HidGetDescriptor(
    _In_ HidDevice_t *Device,
    _Out_ UsbHidDescriptor_t *Descriptor)
{
    // Perform the descriptor retrieving
    UsbTransferStatus_t Status = UsbExecutePacket(Device->Base.DriverId, 
        Device->Base.DeviceId, &Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, DESCRIPTOR_TYPE_HID, 0, 
        (uint16_t)Device->Base.Interface.Id,
        sizeof(UsbHidDescriptor_t), (void*)Descriptor);

    if (Status != TransferFinished) {
        ERROR("HidGetDescriptor failed with code %u", Status);
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* HidGetReportDescriptor 
 * Retrieves the full report descriptor that describes the different
 * available input points of the HID */
OsStatus_t
HidGetReportDescriptor(
    _In_ HidDevice_t *Device,
    _In_ uint8_t ReportType,
    _In_ uint8_t ReportLength,
    _Out_ uint8_t *ReportBuffer)
{
    // Perform the descriptor retrieving
    UsbTransferStatus_t Status = UsbExecutePacket(Device->Base.DriverId, 
        Device->Base.DeviceId, &Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, ReportType, 0, 
        (uint16_t)Device->Base.Interface.Id,
        ReportLength, (void*)ReportBuffer);

    if (Status != TransferFinished) {
        ERROR("HidGetReportDescriptor failed with code %u", Status);
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* HidSetProtocol
 * Changes the current protocol of the device. 
 * 0 = Boot Protocol, 1 = Report Protocol */
OsStatus_t
HidSetProtocol(
    _In_ HidDevice_t *Device,
    _In_ int Protocol)
{
    if (UsbExecutePacket(Device->Base.DriverId, Device->Base.DeviceId,
        &Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS,
        HID_SET_PROTOCOL, 0, Protocol & 0xFF, 
        (uint16_t)Device->Base.Interface.Id, 0, NULL) != TransferFinished) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* HidSetIdle
 * Changes the current situation of the device to idle. 
 * Set ReportId = 0 to apply to all reports. 
 * Set Duration = 0 to apply indefinite duration. Use this
 * to set the report time-out time, minimum value is device polling rate */
OsStatus_t
HidSetIdle(
    _In_ HidDevice_t *Device,
    _In_ int ReportId,
    _In_ int Duration)
{
    // This request may stall, which means it's unsupported
    if (UsbExecutePacket(Device->Base.DriverId, Device->Base.DeviceId,
        &Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_INTERFACE | USBPACKET_DIRECTION_CLASS,
        HID_SET_IDLE, Duration & 0xFF, ReportId & 0xFF, 
        (uint16_t)Device->Base.Interface.Id, 0, NULL) == TransferFinished) {
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/* HidSetupGeneric 
 * Sets up a generic HID device like a mouse or a keyboard. */
OsStatus_t
HidSetupGeneric(
    _In_ HidDevice_t *Device)
{
    // Variables
    UsbHidDescriptor_t Descriptor;
    uint8_t *ReportDescriptor = NULL;
    size_t ReportLength = 0;

    // Retrieve the hid descriptor
    if (HidGetDescriptor(Device, &Descriptor) != OsSuccess) {
        ERROR("Failed to retrieve the default hid descriptor.");
        return OsError;
    }

    // Switch to report protocol
    if (Device->Base.Interface.Subclass == HID_SUBCLASS_BOOT) {
        if (HidSetProtocol(Device, 1) != OsSuccess) {
            ERROR("Failed to set the hid device into report protocol.");
            return OsError;
        }
    }

    // Put the device into idle-state
    // We might have to set Duration to 500 ms for keyboards, but has to be tested
    // time is calculated in 4ms resolution, so 500ms = Duration = 125
    if (HidSetIdle(Device, 0, 0) != OsSuccess) {
        WARNING("SetIdle failed, it might be unsupported by the HID.");
    }

    // Retrieve the report descriptor
    ReportDescriptor = (uint8_t*)malloc(Descriptor.ClassDescriptorLength);
    if (HidGetReportDescriptor(Device, Descriptor.ClassDescriptorType,
        Descriptor.ClassDescriptorLength, ReportDescriptor) != OsSuccess) {
        ERROR("Failed to retrieve the report descriptor.");
        free(ReportDescriptor);
        return OsError;
    }

    // Parse the report descriptor
    ReportLength = HidParseReportDescriptor(Device, 
        ReportDescriptor, Descriptor.ClassDescriptorLength);
        
    // Cleanup unneeded descriptor
    free(ReportDescriptor);

    // Store the length of the report
    Device->ReportLength = ReportLength;
    return OsSuccess;
}
