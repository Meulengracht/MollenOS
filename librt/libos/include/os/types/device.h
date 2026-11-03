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
    OSIOCTLREQUEST_IO_REQUIREMENTS     = 1 | __IOCTL_IN,
    OSIOCTLREQUEST_PCI_INTERRUPT_QUERY = 2 | __IOCTL_IN,
    OSIOCTLREQUEST_PCI_INTERRUPT       = 3,
    // Controls a platform interrupt source with DeviceInterruptSourceRequest.
    OSIOCTLREQUEST_INTERRUPT_CONTROL   = 4,
    // Queries one source by firmware index using DeviceInterruptSourceQuery.
    OSIOCTLREQUEST_INTERRUPT_QUERY     = 5 | __IOCTL_IN,
    // Copies registered IO resources without mapping them into the driver.
    OSIOCTLREQUEST_DEVICE_RESOURCES    = 6 | __IOCTL_IN,
    // Prepares register access. Enabling and stopping DMA are separate steps.
    OSIOCTLREQUEST_DEVICE_ACTIVATE     = 7,
    // Keeps the configured DMA path until the caller closes DMA and releases it.
    OSIOCTLREQUEST_DMA_QUERY           = 8 | __IOCTL_IN,
    OSIOCTLREQUEST_DMA_RELEASE         = 9,
    // Copies MAC/PHY wiring and reset timings without changing hardware.
    OSIOCTLREQUEST_ETHERNET_CONFIG     = 10 | __IOCTL_IN,
    // Requires a prepared device and a zero uint32_t. Pulses the firmware reset line.
    OSIOCTLREQUEST_ETHERNET_PHY_RESET  = 11
};

/**
 * @brief Bus control flags for a device.
 */
struct OSIOCtlBusControl {
    unsigned int Flags;
};

/**
 * @brief Buffer requirements for a device's IO requests.
 * The IO manager uses these requirements when submitting requests to the device.
 */
struct OSIOCtlRequestRequirements {
    uint32_t                BufferAlignment;
    enum OSMemoryConformity Conformity;
};

#endif //!__OS_TYPES_DEVICE_H__
