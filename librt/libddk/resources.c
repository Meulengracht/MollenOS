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

#include <ddk/resources.h>
#include <os/device.h>

oserr_t
DeviceGetResources(
    _In_  uuid_t                  device,
    _Out_ struct DeviceResources* resources)
{
    struct DeviceResources result = { 0 };
    oserr_t                status;

    if (resources == NULL) {
        return OS_EINVALPARAMS;
    }

    status = OSDeviceIOCtl(
        device,
        OSIOCTLREQUEST_DEVICE_RESOURCES,
        &result,
        sizeof(result)
    );
    if (status != OS_EOK) {
        return status;
    }
    
    if (result.Count > DEVICE_MAX_REGISTERS) {
        return OS_EPROTOCOL;
    }
    
    *resources = result;
    return OS_EOK;
}

oserr_t
DeviceActivate(
    _In_ uuid_t                device,
    _In_ enum DeviceActivation stage)
{
    return OSDeviceIOCtl(device, OSIOCTLREQUEST_DEVICE_ACTIVATE, &stage, sizeof(stage));
}
