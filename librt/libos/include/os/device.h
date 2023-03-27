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

#ifndef __OS_DEVICE_H__
#define __OS_DEVICE_H__

#include <os/types/device.h>

/**
 * @brief Allows controlling certain aspects of a device. Not all I/O requests
 * go to the specific device, some may be intercepted by the devicemanager.
 * @param deviceId
 * @param request
 * @param buffer
 * @param length
 * @return
 */
CRTDECL(oserr_t,
OSDeviceIOCtl(
        _In_ uuid_t              deviceId,
        _In_ enum OSIOCtlRequest request,
        _In_ void*               buffer,
        _In_ size_t              length));

#endif //!__OS_FUTEX_H__
