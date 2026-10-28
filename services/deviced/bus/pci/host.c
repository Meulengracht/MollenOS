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
#include <firmware/tree.h>
#include <core/io.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <threads.h>

list_t        g_pciDevices = LIST_INIT;
list_t        g_pciRoots = LIST_INIT;

static mtx_t  g_pciDevicesLock;
static uuid_t g_nextPciHostId = 1;

static int g_pciInitialized;

void
PciInitialize(
    _In_ const struct PciFunctionHandler* const* handlers,
    _In_ size_t                                handlerCount)
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
    
    PciFunctionHandlersInitialize(handlers, handlerCount);
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
        if ((unsigned int)device->Host->Identification.Segment != segment) {
            continue;
        }
        if (device->Bus == bus && device->Slot == slot && device->Function == function) {
            return device;
        }
    }
    return NULL;
}

static oserr_t
__PciDestroyDevice(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    TRACE("__PciDestroyDevice(%u:%u:%u.%u)", device->Host->Identification.Segment,
        device->Bus, device->Slot, device->Function);

    // Remove child devices first. If an attachment is busy, leave that device
    // and its parents allocated. A retry resumes after earlier removals.
    while (device->Children.head != NULL) {
        status = __PciDestroyDevice(device->Children.head->value);
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
    
    list_remove(&device->Parent->Children, &device->ChildHeader);
    list_remove(&g_pciDevices, &device->ListHeader);
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
    
    foreach (element, &device->Children) {
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
    
    // Callers must stop this host's clients before teardown. Removing device
    // manager entries can cancel pending discovery requests, so the PCI lookup
    // lock must be released while those callbacks run.
    if (bus->RootDevice != NULL && !bus->Removing) {
        status = PciUnpublishDevice(bus->RootDevice);
        if (status != OS_EOK) {
            WARNING("PciHostDestroy host %u remains published: %u",
                bus->Identification.HostId, status);
            return status;
        }
        bus->Removing = 1;
    }

    PciCriticalSectionEnter();
    
    if (bus->RootDevice != NULL) {
        if (__PciHasProviderReferences(bus->RootDevice)) {
            PciCriticalSectionLeave();
            WARNING("PciHostDestroy host %u still has device owners",
                bus->Identification.HostId);
            return OS_EBUSY;
        }
        
        while (bus->RootDevice->Children.head != NULL) {
            status = __PciDestroyDevice(bus->RootDevice->Children.head->value);
            if (status != OS_EOK) {
                PciCriticalSectionLeave();
                return status;
            }
        }
    }
    
    if (bus->Operations != NULL && bus->Operations->Destroy != NULL) {
        status = bus->Operations->Destroy(bus);
        if (status != OS_EOK) {
            PciCriticalSectionLeave();
            return status;
        }
    }
    bus->Operations = NULL;

    status = DmIoRelease(&bus->IoSpace, &bus->IoAcquired);
    if (status != OS_EOK) {
        PciCriticalSectionLeave();
        return status;
    }
    // Keep the host's bus range reserved until every owned resource is gone.
    if (bus->RootDevice != NULL) {
        list_remove(&g_pciRoots, &bus->RootDevice->ListHeader);
        free(bus->RootDevice);
    }
    FdtTreeRelease(bus->FirmwareTree);
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
    
    list_construct(&root->Children);
    ELEMENT_INIT(&root->ListHeader, (uintptr_t)identification->HostId, root);
    root->Host = host;
    root->Bus = identification->BusStart;
    root->IsBridge = 1;
    host->RootDevice = root;
    list_append(&g_pciRoots, &root->ListHeader);
    
    PciCriticalSectionLeave();
    return OS_EOK;
}

oserr_t
PciHostAttach(
    _In_ PciHost_t*      bus,
    _In_ struct FdtTree* tree)
{
    oserr_t status;

    TRACE("PciHostAttach(segment=%u, buses=%u-%u)", bus->Identification.Segment,
        bus->Identification.BusStart, bus->Identification.BusEnd);

    if (bus->Identification.HostId != UUID_INVALID || bus->FirmwareTree != NULL) {
        WARNING("PciHostAttach host is already attached");
        return OS_EEXISTS;
    }

    // The constructor uses the common firmware tree without adding a reference.
    // Add the host's reference before registration transfers ownership to PCI.
    bus->FirmwareTree = tree;
    if (tree != NULL) {
        FdtTreeAddReference(tree);
    }
    
    status = PciHostRegister(bus);
    if (status != OS_EOK) {
        ERROR("PCI host registration failed for segment %u buses %u-%u: %u",
            bus->Identification.Segment, bus->Identification.BusStart,
            bus->Identification.BusEnd, status);
        bus->FirmwareTree = NULL;
        FdtTreeRelease(tree);
    }
    return status;
}
