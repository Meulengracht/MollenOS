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

#include <ddk/devices/device.h>
#include <ddk/io.h>

_CODE_BEGIN

#define DEVICE_MAX_REGISTERS 8

/**
 * @brief Identity of a child's immediate PCI parent, supplied by its resource provider.
 * VendorId zero means unavailable. This identifies the immediate PCI parent,
 * not the device itself or whether a peripheral integration is supported.
 */
struct DeviceParentPciIdentity {
    uint32_t VendorId;
    uint32_t ProductId;
    uint32_t Revision;
};

/**
 * @brief Registered IO descriptors and parent identity.
 * The provider supplies register mappings on request.
 * ParentPci is zero when the provider cannot describe an immediate PCI parent.
 * This is the DEVICE_RESOURCES ioctl layout; rebuild providers and clients
 * together when changing it.
 */
struct DeviceResources {
    uint32_t                       Count;
    DeviceIo_t                     Registers[DEVICE_MAX_REGISTERS];
    struct DeviceParentPciIdentity ParentPci;
};

enum DeviceActivation {
    // Enable register access and provider-owned bus or power dependencies.
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
 * @return Provider status; failure leaves the driver responsible for cleanup.
 */
DDKDECL(oserr_t,
DeviceActivate(
    _In_ uuid_t                device,
    _In_ enum DeviceActivation stage));

_CODE_END

#endif
