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

#include "private.h"
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
    if (g_pciInitialized) {
        return;
    }

    mtx_init(&g_pciDevicesLock, mtx_plain);
    g_pciInitialized = 1;
}

void
PciCriticalSectionEnter(void)
{
    mtx_lock(&g_pciDevicesLock);
}

void
PciCriticalSectionLeave(void)
{
    mtx_unlock(&g_pciDevicesLock);
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
    if (mapping != NULL && --mapping->References == 0) {
        FirmwareTableUnmap(mapping->Blob, mapping->Length);
        free(mapping);
    }
}

static void
__PciDestroyDevice(
    _In_ PciDevice_t* device)
{
    while (device->children.head != NULL) {
        __PciDestroyDevice(device->children.head->value);
    }
    
    list_remove(&device->Parent->children, &device->child_header);
    list_remove(&g_pciDevices, &device->list_header);
    
    if (device->Attachment != NULL) {
        device->Handler->Destroy(device->Attachment);
    }
    free(device->Header);
    free(device);
}

oserr_t
PciHostDestroy(
    _In_ PciHost_t* bus)
{
    oserr_t status;

    if (bus == NULL) {
        return OS_EOK;
    }
    
    // Removing device-manager entries can cancel pending discovery requests.
    // Do not hold the PCI lookup lock while those callbacks run. Callers must
    // stop this host's clients before teardown.
    if (bus->RootDevice != NULL) {
        status = PciUnpublishDevice(bus->RootDevice);
        if (status != OS_EOK) {
            return status;
        }
    }

    // Callers have stopped this host's clients before its tree is removed.
    PciCriticalSectionEnter();
    
    if (bus->RootDevice != NULL) {
        while (bus->RootDevice->children.head != NULL) {
            __PciDestroyDevice(bus->RootDevice->children.head->value);
        }
        list_remove(&g_pciRoots, &bus->RootDevice->list_header);
        free(bus->RootDevice);
        bus->RootDevice = NULL;
    }
    
    if (bus->Operations != NULL && bus->Operations->Destroy != NULL) {
        bus->Operations->Destroy(bus);
    }
    
    ReleaseDeviceIo(&bus->IoSpace);
    DestroyDeviceIo(&bus->IoSpace);
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

    if (host == NULL || host->Operations == NULL) {
        return OS_EINVALPARAMS;
    }
    if (host->Operations->Read == NULL || host->Operations->Write == NULL) {
        return OS_EINVALPARAMS;
    }
    
    identification = &host->Identification;
    if (identification->BusStart > identification->BusEnd) {
        return OS_EINVALPARAMS;
    }

    PciCriticalSectionEnter();
    if (identification->HostId != UUID_INVALID || host->RootDevice != NULL) {
        PciCriticalSectionLeave();
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
            return OS_EEXISTS;
        }
    }
    
    if (g_nextPciHostId == UUID_INVALID) {
        PciCriticalSectionLeave();
        return OS_EOVERFLOW;
    }

    root = calloc(1, sizeof(PciDevice_t));
    if (root == NULL) {
        PciCriticalSectionLeave();
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

    if (bus->Identification.HostId != UUID_INVALID || bus->FirmwareMapping != NULL) {
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
