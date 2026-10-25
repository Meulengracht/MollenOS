/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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

#ifndef __DDK_RESOURCES_H__
#define __DDK_RESOURCES_H__

#include <ddk/device.h>
#include <ddk/io.h>

_CODE_BEGIN

#define DEVICE_MAX_REGISTERS 8

/** Registered IO descriptors; acquiring a descriptor supplies its CPU mapping. */
struct DeviceResources {
    uint32_t Count;
    DeviceIo_t Registers[DEVICE_MAX_REGISTERS];
};

enum DeviceActivation {
    // Enable register access and complete provider-specific peripheral setup.
    DevicePrepare,
    // Permit DMA after the driver has prepared buffers and interrupts.
    DeviceStart,
    // Finish provider shutdown after the driver has halted DMA.
    DeviceStop
};

/**
 * @brief Query provider-owned register descriptors without mapping them.
 * @param device Registered device identity.
 * @param resources Output populated only on success.
 * @return Provider status. No physical-address fallback is performed.
 */
DDKDECL(oserr_t,
DeviceGetResources(
    _In_  uuid_t                  device,
    _Out_ struct DeviceResources* resources));

/**
 * @brief Change the device activation stage through its provider.
 * @param device Registered device identity.
 * @param stage Prepare, start or stop; repeated requests are idempotent.
 * @return Provider status; failure retains the driver's cleanup obligation.
 */
DDKDECL(oserr_t,
DeviceActivate(
    _In_ uuid_t                device,
    _In_ enum DeviceActivation stage));

_CODE_END

#endif
