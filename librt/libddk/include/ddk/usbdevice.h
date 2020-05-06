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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Contract Definitions & Structures (Usb-Device Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_USBDEVICE_H__
#define __DDK_USBDEVICE_H__

#include <ddk/ddkdefs.h>
#include <ddk/device.h>
#include <usb/usb.h>

typedef struct UsbDevice {
    Device_t             Base;
    usb_device_context_t DeviceContext;
} UsbDevice_t;

#endif
