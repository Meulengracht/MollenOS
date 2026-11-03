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
#include <ddk/ethernet.h>
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

    // Query into temporary storage so a transport error or invalid reply does
    // not replace the caller's existing resource description.
    status = OSDeviceIOCtl(
        device,
        OSIOCTLREQUEST_DEVICE_RESOURCES,
        &result,
        sizeof(result)
    );
    if (status != OS_EOK) {
        return status;
    }
    
    // The returned count must fit the fixed array before a driver uses it.
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

oserr_t
DeviceGetEthernetConfiguration(
    _In_  uuid_t                              device,
    _Out_ struct DeviceEthernetConfiguration* configuration)
{
    struct DeviceEthernetConfiguration result = { 0 };
    oserr_t                            status;

    if (configuration == NULL) {
        return OS_EINVALPARAMS;
    }

    // Firmware configuration can be queried before preparing the hardware.
    // Keep the result local until both the request and reply checks succeed.
    status = OSDeviceIOCtl(device, OSIOCTLREQUEST_ETHERNET_CONFIG, &result, sizeof(result));
    if (status != OS_EOK) {
        return status;
    }

    // Do not pass an unknown layout, invalid MDIO address or unsupported mode
    // to the driver, where it could lead to programming the wrong PHY settings.
    if (result.Version != 1 || result.PhyAddress > 31 ||
        result.Mode < DeviceEthernetMii || result.Mode > DeviceEthernetRgmiiTxId) {
        return OS_EPROTOCOL;
    }

    *configuration = result;
    return OS_EOK;
}

oserr_t
DeviceResetEthernetPhy(
    _In_ uuid_t device)
{
    // The wire request requires a zero word even though reset has no arguments.
    // GPIO selection and pulse timing come from the validated firmware data.
    uint32_t reserved = 0;

    return OSDeviceIOCtl(device, OSIOCTLREQUEST_ETHERNET_PHY_RESET, &reserved, sizeof(reserved));
}
