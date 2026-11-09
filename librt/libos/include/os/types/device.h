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
    // Controls a platform interrupt source with DeviceInterruptSourceRequest.
    OSIOCTLREQUEST_INTERRUPT_CONTROL   = 3,
    // Queries one source by firmware index using DeviceInterruptSourceQuery.
    OSIOCTLREQUEST_INTERRUPT_QUERY     = 4 | __IOCTL_IN,
    // Copies registered IO resources without mapping them into the driver.
    OSIOCTLREQUEST_DEVICE_RESOURCES    = 5 | __IOCTL_IN,
    // Prepares register access. Enabling and stopping DMA are separate steps.
    OSIOCTLREQUEST_DEVICE_ACTIVATE     = 6,
    // Keeps the configured DMA path until the caller closes DMA and releases it.
    OSIOCTLREQUEST_DMA_QUERY           = 7 | __IOCTL_IN,
    OSIOCTLREQUEST_DMA_RELEASE         = 8,
    // Copies MAC/PHY wiring and reset timings without changing hardware.
    OSIOCTLREQUEST_ETHERNET_CONFIG     = 9 | __IOCTL_IN,
    // Requires a prepared device and a zero uint32_t. Pulses the firmware reset line.
    OSIOCTLREQUEST_ETHERNET_PHY_RESET  = 10
};

/**
 * @brief Bus control flags for a device.
 */
struct OSIOCtlBusControl {
    unsigned int Flags;
};


#endif //!__OS_TYPES_DEVICE_H__
