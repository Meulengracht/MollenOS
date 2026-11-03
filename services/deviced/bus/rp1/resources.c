/**
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
 */

#include <bus/rp1/rp1.h>
#include <bus/rp1/ethernet.h>
#include <bus/pci/device.h>
#include <core/io.h>
#include <firmware/ethernet.h>
#include <ddk/utils.h>

oserr_t
Rp1ChildResourceControl(
    _InOut_ struct Rp1Child*    child,
    _In_    uuid_t              principal,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length)
{
    struct DeviceResources*  resources = &child->Resources.Published;
    const PciNativeHeader_t* parent;
    oserr_t                  status;

    if (!child->Bus->Ready) {
        WARNING("Rp1ChildResourceControl: device %u bus is not ready for request 0x%x",
                child->DeviceId, request);
        return OS_EBUSY;
    }

    // Allow firmware configuration queries before preparing Ethernet. Clock
    // setup needs the same wiring information.
    if (request == OSIOCTLREQUEST_ETHERNET_CONFIG) {
        if (buffer == NULL || length != sizeof(struct DeviceEthernetConfiguration)) {
            return OS_EINVALPARAMS;
        }
        return FdtEthernetConfiguration(child->Bus->Host->Tree, child->Firmware.NodeOffset, buffer);
    }

    // GEM needs clock setup and reset validation. Handle its requests before
    // the generic child path, which only records preparation state.
    if (request == OSIOCTLREQUEST_ETHERNET_PHY_RESET ||
        request == OSIOCTLREQUEST_DEVICE_ACTIVATE) {
        if (Rp1EthernetIsDevice(child)) {
            return Rp1EthernetControl(child, principal, request, buffer, length);
        }
    }

    if (request == OSIOCTLREQUEST_DEVICE_ACTIVATE) {
        enum DeviceActivation stage;
        
        // For children other than GEM, only record preparation state here for
        // the start and stop checks. These requests do not change hardware.
        if (buffer == NULL || length != sizeof(enum DeviceActivation)) {
            ERROR("Rp1ChildResourceControl: device %u invalid activation buffer %p, length %zu, expected %zu",
                  child->DeviceId, buffer, length, sizeof(enum DeviceActivation));
            return OS_EINVALPARAMS;
        }

        stage = *(enum DeviceActivation*)buffer;
        if (stage == DevicePrepare) {
            child->Resources.Prepared = 1;
            return OS_EOK;
        }
        
        // The child driver must halt its peripheral before DeviceStop. RP1 bus
        // mastering stays enabled because other children may still use DMA.
        if (stage == DeviceStop) {
            child->Resources.Prepared = 0;
            return OS_EOK;
        }
        
        if (stage == DeviceStart) {
            if (!child->Resources.Prepared) {
                WARNING("Rp1ChildResourceControl: device %u cannot start before DevicePrepare",
                        child->DeviceId);
                return OS_EBUSY;
            }
            return OS_EOK;
        }

        ERROR("Rp1ChildResourceControl: device %u unknown activation stage %u",
              child->DeviceId, stage);
        return OS_EINVALPARAMS;
    }

    if (request != OSIOCTLREQUEST_DEVICE_RESOURCES) {
        TRACE("Rp1ChildResourceControl: device %u unsupported request 0x%x",
              child->DeviceId, request);
        return OS_ENOTSUPPORTED;
    }

    if (buffer == NULL || length != sizeof(struct DeviceResources)) {
        ERROR("Rp1ChildResourceControl: device %u invalid resource buffer %p, length %zu, expected %zu",
              child->DeviceId, buffer, length, sizeof(struct DeviceResources));
        return OS_EINVALPARAMS;
    }

    if (child->Firmware.RegisterCount > DEVICE_MAX_REGISTERS) {
        ERROR("Rp1ChildResourceControl: device %u has %u registers, limit is %u",
              child->DeviceId, child->Firmware.RegisterCount, (unsigned int)DEVICE_MAX_REGISTERS);
        return OS_ENOTSUPPORTED;
    }

    // Resources created before an error stay recorded for retry or destruction.
    // Skip them when a later query resumes setup.
    for (unsigned int i = 0; i < child->Firmware.RegisterCount; i++) {
        const struct FdtRp1Range* range;
        
        if (resources->Registers[i].Id != UUID_INVALID) {
            continue;
        }

        range = &child->Firmware.Registers[i];
        status = DmIoCreateMemory(
            &resources->Registers[i],
            range->Base,
            range->Length
        );
        if (status != OS_EOK) {
            ERROR("Rp1ChildResourceControl: device %u could not create memory register %u (%u)",
                  child->DeviceId, i, status);
            return status;
        }
    }

    parent = child->Bus->Parent->Header;
    resources->ParentPci.VendorId = parent->VendorId;
    resources->ParentPci.ProductId = parent->DeviceId;
    resources->ParentPci.Revision = parent->Revision;
    resources->Count = child->Firmware.RegisterCount;

    *(struct DeviceResources*)buffer = *resources;
    return OS_EOK;
}

oserr_t
Rp1ChildResourcesDestroy(
    _InOut_ struct Rp1Child* child)
{
    oserr_t status;

    // Before destroying the child's public handles, restore the Ethernet clock
    // and GPIO settings. If cleanup fails, keep the child so it can be retried.
    status = Rp1EthernetRelease(child);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DmIoDestroy(child->Resources.Published.Registers, DEVICE_MAX_REGISTERS);
    if (status != OS_EOK) {
        ERROR("Rp1ChildResourcesDestroy: device %u could not destroy registered resources (%u)",
              child->DeviceId, status);
        return status;
    }
    return OS_EOK;
}
