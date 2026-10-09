/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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
 */

//#define __TRACE

#include <bus/legacy/fixed.h>
#include <devices.h>
#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <stdlib.h>

oserr_t
__InstallFixedBusDevice(
    _In_ BusDevice_t* device,
    _In_ const char*  Description)
{
    uuid_t Id;

    device->Base.ParentId = UUID_INVALID;
    device->Base.Length   = sizeof(BusDevice_t);
    device->Base.VendorId = PCI_FIXED_VENDORID;

    // Set more magic constants to ignore class and subclass
    device->Base.Class    = 0xFF0F;
    device->Base.Subclass = 0xFF0F;
    device->Base.Identification.Description = strdup(Description);

    // Invalidate irqs, this must be set by fixed drivers
    device->InterruptPin         = INTERRUPT_NONE;
    device->InterruptLine        = INTERRUPT_NONE;
    device->InterruptAcpiConform = 0;
    return DmDeviceCreate(&device->Base, DEVICE_REGISTER_FLAG_LOADDRIVER, &Id);
}

oserr_t
__InstallPS2Controller(void)
{
    BusDevice_t* device;
    oserr_t      oserr;

    device = malloc(sizeof(BusDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }
    memset(device, 0, sizeof(BusDevice_t));

    // Set default ps2 device settings
    device->Base.ProductId = PCI_PS2_DEVICEID;

    // Register io-spaces for the ps2 controller, it has two ports
    // Data port - 0x60
    // oserr/Command port - 0x64
    // one byte each
    oserr = CreateDevicePortIo(&device->IoSpaces[0], 0x60, 1);
    if (oserr != OS_EOK) {
        ERROR(" > failed to initialize ps2 data io space");
        return OS_EUNKNOWN;
    }

    oserr = CreateDevicePortIo(&device->IoSpaces[1], 0x64, 1);
    if (oserr != OS_EOK) {
        ERROR(" > failed to initialize ps2 command/status io space");
        return OS_EUNKNOWN;
    }
    return __InstallFixedBusDevice(device, "PS/2 Controller");
}
