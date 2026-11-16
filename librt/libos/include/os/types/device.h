/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_TYPES_DEVICE_H__
#define	__OS_TYPES_DEVICE_H__

#include <os/types/memory.h>

#define __IOCTL_IN 0x10000

enum OSIOCtlRequest {
    OSIOCTLREQUEST_BUS_CONTROL         = 0,
    OSIOCTLREQUEST_PCI_INTERRUPT_QUERY = 1 | __IOCTL_IN,
    OSIOCTLREQUEST_PCI_INTERRUPT       = 2,
    // Reserves, enables, disables or releases one platform interrupt source.
    // The argument is a DeviceInterruptSourceRequest.
    OSIOCTLREQUEST_INTERRUPT_CONTROL   = 3,
    // Reads the details of one interrupt source. The source is picked by its index
    // in the firmware tables. The argument is a DeviceInterruptSourceQuery.
    OSIOCTLREQUEST_INTERRUPT_QUERY     = 4 | __IOCTL_IN,
    // Returns the list of IO resources registered for the device, such as register
    // ranges. Nothing is mapped into the driver by this request.
    OSIOCTLREQUEST_DEVICE_RESOURCES    = 5 | __IOCTL_IN,
    // Moves the device to a new stage. The argument is the stage value (prepare,
    // start or stop). Prepare only turns on register access. Start also lets the
    // device do DMA, and stop turns DMA off again.
    OSIOCTLREQUEST_DEVICE_ACTIVATE     = 6,
    // Returns the MAC and PHY wiring and the PHY reset timings from firmware data.
    // Does not touch the hardware, so it works before the device is prepared.
    OSIOCTLREQUEST_ETHERNET_CONFIG     = 7 | __IOCTL_IN,
    // Pulses the PHY reset line described by firmware. The device must be prepared
    // first. The argument must be one uint32_t set to zero.
    OSIOCTLREQUEST_ETHERNET_PHY_RESET  = 8
};

/**
 * @brief Bus control flags for a device.
 */
struct OSIOCtlBusControl {
    unsigned int Flags;
};

#endif //!__OS_TYPES_DEVICE_H__
