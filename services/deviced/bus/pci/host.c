/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

//#define __TRACE

#include <bus/pci/host-private.h>
#include <bus/pci/device.h>
#include <bus/pci/function.h>
#include <bus/pci/publish.h>
#include <ddk/firmware.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <threads.h>

list_t        g_pciDevices = LIST_INIT;
list_t        g_pciRoots = LIST_INIT;

static mtx_t  g_pciDevicesLock;
static uuid_t g_nextPciHostId = 1;

static int g_pciInitialized;

void
PciInitialize(void)
{
    int status;

    TRACE("PciInitialize()");

    if (g_pciInitialized) {
        return;
    }

    status = mtx_init(&g_pciDevicesLock, mtx_plain);
    if (status != thrd_success) {
        ERROR("PciInitialize failed to initialize the device lock: %i", status);
    }
    g_pciInitialized = 1;
}

const struct PciHostIdentification*
PciHostGetIdentification(
    _In_ const PciHost_t* host)
{
    return &host->Identification;
}

void
PciCriticalSectionEnter(void)
{
    int status;

    status = mtx_lock(&g_pciDevicesLock);
    if (status != thrd_success) {
        ERROR("PciCriticalSectionEnter failed to lock devices: %i", status);
    }
}

void
PciCriticalSectionLeave(void)
{
    int status;

    status = mtx_unlock(&g_pciDevicesLock);
    if (status != thrd_success) {
        ERROR("PciCriticalSectionLeave failed to unlock devices: %i", status);
    }
}

PciDevice_t*
PciFindDevice(
    _In_ unsigned int segment,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function)
{
    PciDevice_t* device;

    foreach (element, &g_pciDevices) {
        device = element->value;
        if ((unsigned int)device->Host->Identification.Segment == segment &&
            device->Bus == bus && device->Slot == slot && device->Function == function) {
            return device;
        }
    }
    return NULL;
}

void
PciFirmwareRelease(
    _In_ struct PciFirmwareMapping* mapping)
{
    oserr_t status;

    if (mapping != NULL && --mapping->References == 0) {
        TRACE("PciFirmwareRelease releasing the last firmware mapping reference");
        status = FirmwareTableUnmap(mapping->Blob, mapping->Length);
        if (status != OS_EOK) {
            ERROR("PciFirmwareRelease failed to unmap firmware: %u", status);
        }
        free(mapping);
    }
}

static oserr_t
__PciDestroyDevice(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    TRACE("__PciDestroyDevice(%u:%u:%u.%u)", device->Host->Identification.Segment,
        device->Bus, device->Slot, device->Function);

    // Remove descendants first; a busy attachment keeps its enclosing objects
    // alive. Earlier successful removals stay removed when the owner retries.
    while (device->children.head != NULL) {
        status = __PciDestroyDevice(device->children.head->value);
        if (status != OS_EOK) {
            WARNING("__PciDestroyDevice child teardown incomplete: %u", status);
            return status;
        }
    }

    if (device->Attachment != NULL) {
        status = device->Handler->Destroy(device->Attachment);
        if (status != OS_EOK) {
            WARNING("__PciDestroyDevice attachment teardown incomplete: %u", status);
            return status;
        }
    }
    
    list_remove(&device->Parent->children, &device->child_header);
    list_remove(&g_pciDevices, &device->list_header);
    free(device->Header);
    free(device);
    return OS_EOK;
}

static int
__PciHasProviderReferences(
    _In_ PciDevice_t* device)
{
    if (device->ProviderReferences != 0 || device->Msi.Active || device->Msix != NULL) {
        return 1;
    }
    
    foreach (element, &device->children) {
        if (__PciHasProviderReferences(element->value)) {
            return 1;
        }
    }
    return 0;
}

oserr_t
PciHostDestroy(
    _In_ PciHost_t* bus)
{
    oserr_t status;

    TRACE("PciHostDestroy(host=%p)", bus);

    if (bus == NULL) {
        return OS_EOK;
    }
    
    // Removing device-manager entries can cancel pending discovery requests.
    // Do not hold the PCI lookup lock while those callbacks run. Callers must
    // stop this host's clients before teardown.
    if (bus->RootDevice != NULL) {
        status = PciUnpublishDevice(bus->RootDevice);
        if (status != OS_EOK) {
            WARNING("PciHostDestroy host %u remains published: %u",
                bus->Identification.HostId, status);
            return status;
        }
    }

    // Callers have stopped this host's clients before its tree is removed.
    PciCriticalSectionEnter();
    
    if (bus->RootDevice != NULL) {
        if (__PciHasProviderReferences(bus->RootDevice)) {
            PciCriticalSectionLeave();
            WARNING("PciHostDestroy host %u still has device owners",
                bus->Identification.HostId);
            return OS_EBUSY;
        }
        
        while (bus->RootDevice->children.head != NULL) {
            status = __PciDestroyDevice(bus->RootDevice->children.head->value);
            if (status != OS_EOK) {
                PciCriticalSectionLeave();
                return status;
            }
        }
        
        list_remove(&g_pciRoots, &bus->RootDevice->list_header);
        free(bus->RootDevice);
        bus->RootDevice = NULL;
    }
    
    if (bus->Operations != NULL && bus->Operations->Destroy != NULL) {
        bus->Operations->Destroy(bus);
    }
    
    status = ReleaseDeviceIo(&bus->IoSpace);
    if (status != OS_EOK) {
        ERROR("PciHostDestroy failed to release configuration I/O: %u", status);
    }
    status = DestroyDeviceIo(&bus->IoSpace);
    if (status != OS_EOK) {
        ERROR("PciHostDestroy failed to destroy configuration I/O: %u", status);
    }
    PciFirmwareRelease(bus->FirmwareMapping);
    free(bus);

    PciCriticalSectionLeave();
    return OS_EOK;
}

oserr_t
PciHostRegister(
    _In_ PciHost_t* host)
{
    PciDevice_t*                        root;
    PciHost_t*                          existing;
    const struct PciHostIdentification* identification;

    TRACE("PciHostRegister(host=%p)", host);

    if (host == NULL || host->Operations == NULL) {
        ERROR("PciHostRegister missing host or operations");
        return OS_EINVALPARAMS;
    }
    if (host->Operations->Read == NULL || host->Operations->Write == NULL) {
        ERROR("PciHostRegister missing configuration access callbacks");
        return OS_EINVALPARAMS;
    }
    
    identification = &host->Identification;
    if (identification->BusStart > identification->BusEnd) {
        ERROR("PciHostRegister inverted bus range %u-%u",
            identification->BusStart, identification->BusEnd);
        return OS_EINVALPARAMS;
    }

    PciCriticalSectionEnter();
    if (identification->HostId != UUID_INVALID || host->RootDevice != NULL) {
        PciCriticalSectionLeave();
        WARNING("PciHostRegister host is already registered");
        return OS_EEXISTS;
    }
    
    foreach (element, &g_pciRoots) {
        existing = ((PciDevice_t*)element->value)->Host;
        if (existing->Identification.Segment != identification->Segment) {
            continue;
        }
        if (identification->BusStart <= existing->Identification.BusEnd &&
            existing->Identification.BusStart <= identification->BusEnd) {
            PciCriticalSectionLeave();
            WARNING("PciHostRegister segment %u buses %u-%u overlap an existing host",
                identification->Segment, identification->BusStart, identification->BusEnd);
            return OS_EEXISTS;
        }
    }
    
    if (g_nextPciHostId == UUID_INVALID) {
        PciCriticalSectionLeave();
        ERROR("PciHostRegister host identifiers exhausted");
        return OS_EOVERFLOW;
    }

    root = calloc(1, sizeof(PciDevice_t));
    if (root == NULL) {
        PciCriticalSectionLeave();
        ERROR("PciHostRegister failed to allocate host root");
        return OS_EOOM;
    }

    // Commit identification only after every operation that can fail. IDs are
    // not reused when a host is removed, including across discovery passes.
    host->Identification.HostId = g_nextPciHostId++;
    
    list_construct(&root->children);
    ELEMENT_INIT(&root->list_header, (uintptr_t)identification->HostId, root);
    root->Host = host;
    root->Bus = identification->BusStart;
    root->IsBridge = 1;
    host->RootDevice = root;
    list_append(&g_pciRoots, &root->list_header);
    
    PciCriticalSectionLeave();
    return OS_EOK;
}

oserr_t
PciHostAttach(
    _In_ PciHost_t*                 bus,
    _In_ struct PciFirmwareMapping* mapping)
{
    oserr_t status;

    TRACE("PciHostAttach(segment=%u, buses=%u-%u)", bus->Identification.Segment,
        bus->Identification.BusStart, bus->Identification.BusEnd);

    if (bus->Identification.HostId != UUID_INVALID || bus->FirmwareMapping != NULL) {
        WARNING("PciHostAttach host is already attached");
        return OS_EEXISTS;
    }

    // The constructor borrows the discovery mapping. Establish the host's
    // reference before registration transfers ownership to the PCI subsystem.
    bus->FirmwareMapping = mapping;
    if (mapping != NULL) {
        mapping->References++;
    }
    
    status = PciHostRegister(bus);
    if (status != OS_EOK) {
        ERROR("PCI host registration failed for segment %u buses %u-%u: %u",
            bus->Identification.Segment, bus->Identification.BusStart,
            bus->Identification.BusEnd, status);
        bus->FirmwareMapping = NULL;
        PciFirmwareRelease(mapping);
    }
    return status;
}
