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
//#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include <os/driver/usb.h>
#include "hid.h"

/* Includes
 * - Library */
#include <stdlib.h>

/* HidDeviceCreate
 * Initializes a new hid-device from the given usb-device */
HidDevice_t*
HidDeviceCreate(
    _In_ MCoreUsbDevice_t *UsbDevice)
{
    // Variables
    HidDevice_t *Device = NULL;
    int i;

    // Debug
    TRACE("HidDeviceCreate()");

    // Allocate new resources
    Device = (HidDevice_t*)malloc(sizeof(HidDevice_t));
    memset(Device, 0, sizeof(HidDevice_t));
    memcpy(&Device->Base, UsbDevice, sizeof(MCoreUsbDevice_t));
    Device->Control = &UsbDevice->Endpoints[0];
    Device->TransferId = UUID_INVALID;

    // Find neccessary endpoints
    for (i = 1; i < UsbDevice->Interface.Versions[0].EndpointCount + 1; i++) {
        if (UsbDevice->Endpoints[i].Type == EndpointInterrupt) {
            Device->Interrupt = &UsbDevice->Endpoints[i];
            break;
        }
    }

    // Make sure we at-least found an interrupt endpoint
    if (Device->Interrupt == NULL) {
        ERROR("HID Endpoint (In, Interrupt) did not exist.");
        goto Error;
    }

    // Validate the generic driver
    if (UsbDevice->Interface.Protocol > HID_PROTOCOL_MOUSE) {
        ERROR("This HID uses an unimplemented protocol and needs external drivers %u", 
            UsbDevice->Interface.Protocol);
        goto Error;
    }

    // Setup device
    if (HidSetupGeneric(Device) != OsSuccess) {
        ERROR("Failed to setup the generic hid device.");
        goto Error;
    }

    // Reset interrupt ep
    if (UsbEndpointReset(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Base.Device, Device->Interrupt) != OsSuccess) {
        ERROR("Failed to reset endpoint (interrupt)");
        goto Error;
    }

    // Allocate a ringbuffer for use
    if (BufferPoolAllocate(UsbRetrievePool(), 0x400, 
        &Device->Buffer, &Device->BufferAddress) != OsSuccess) {
        ERROR("Failed to allocate reusable buffer (interrupt-buffer)");
        goto Error;
    }

    // Install interrupt pipe
    UsbTransferInitialize(&Device->Transfer, &Device->Base.Device, 
        Device->Interrupt, InterruptTransfer);
    UsbTransferPeriodic(&Device->Transfer, Device->BufferAddress, 0x400, 
        Device->ReportLength, InTransaction, 1, (__CONST void*)Device);
    if (UsbTransferQueuePeriodic(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Transfer, &Device->TransferId) != OsSuccess) {
        ERROR("Failed to install interrupt transfer");
        goto Error;
    }

    // Done
    return Device;

Error:
    // Cleanup
    if (Device != NULL) {
        HidDeviceDestroy(Device);
    }

    // No device
    return NULL;
}

/* HidDeviceDestroy
 * Destroys an existing hid device instance and cleans up
 * any resources related to it */
OsStatus_t
HidDeviceDestroy(
    _In_ HidDevice_t *Device)
{
    // Destroy the interrupt channel
    if (Device->TransferId != UUID_INVALID) {
        UsbTransferDequeuePeriodic(Device->Base.DriverId, 
            Device->Base.DeviceId, Device->TransferId);
    }

    // Cleanup collections
    
    // Cleanup the buffer
    if (Device->Buffer != NULL) {
        BufferPoolFree(UsbRetrievePool(), Device->Buffer);
    }

    // Cleanup structure
    free(Device);
    return OsSuccess;
}

/* HidInterrupt
 * Should be called from the primary driver OnInterrupt
 * Performs the report-parsing and post-interrupt stuff */
InterruptStatus_t
HidInterrupt(
    _In_ HidDevice_t *Device, 
    _In_ UsbTransferStatus_t Status,
    _In_ size_t DataIndex)
{
    // Sanitize
    if (Device->Collection == NULL || Status == TransferNAK) {
        return InterruptHandled;
    }

    WARNING("HidInterrupt(Status %u, Index %u)", Status, DataIndex);

    // Perform the report parse
    if (!HidParseReport(Device, Device->Collection, DataIndex)) {
        return InterruptHandled;
    }

    // Store previous index
    Device->PreviousDataIndex = DataIndex;
    return InterruptHandled;
}
