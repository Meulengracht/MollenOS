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
    OSIOCTLREQUEST_IO_REQUIREMENTS     = 1 | __IOCTL_IN
};

/**
 * @brief Allows manipulation of certain bus-aspects of an device.
 */
struct OSIOCtlBusControl {
    unsigned int Flags;
};

/**
 * @brief If a device supports IO requests, it can provide io requirements
 * for those requests. The IO manager will then take these into
 * account when an IO request is made for that specific device.
 */
struct OSIOCtlRequestRequirements {
    uint32_t                BufferAlignment;
    enum OSMemoryConformity Conformity;
};

#endif //!__OS_TYPES_DEVICE_H__
