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
 *
 */

#include <ddk/utils.h>
#include <core/io.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/types/pci-interrupt.h>

#include "private.h"

static oserr_t
__PlatformAddReference(
    _In_ void* context)
{
    struct PlatformEntry* entry = context;
    struct PlatformBus*   bus = entry->Bus;
    oserr_t               status = OS_EOK;

    usched_mtx_lock(&bus->Lock);
    if (bus->Removing) {
        status = OS_EBUSY;
    } else {
        atomic_fetch_add(&bus->References, 1);
    }
    usched_mtx_unlock(&bus->Lock);
    return status;
}

static void
__PlatformRelease(
    _In_ void* context)
{
    struct PlatformEntry* entry = context;

    // The bus owns saved IDs and I/O handles until PlatformBusDestroy succeeds.
    atomic_fetch_sub(&entry->Bus->References, 1);
}

static oserr_t
__PlatformResources(
    _InOut_ struct PlatformEntry*   entry,
    _Out_   struct DeviceResources* resources)
{
    const struct PlatformRegister* range;
    oserr_t                        status;

    for (unsigned int i = 0; i < entry->Firmware.RegisterCount; i++) {
        if (entry->Resources.Registers[i].Id != UUID_INVALID) {
            continue;
        }

        range = &entry->Firmware.Registers[i];
        status = DmIoCreateMemory(
            &entry->Resources.Registers[i],
            range->Base,
            range->Length
        );
        if (status != OS_EOK) {
            // Earlier handles remain owned by the inventory for retry or teardown.
            return status;
        }
    }

    entry->Resources.Count = entry->Firmware.RegisterCount;
    *resources = entry->Resources;
    return OS_EOK;
}

static oserr_t
__PlatformControl(
    _In_    void*               context,
    _In_    uuid_t              principal,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length)
{
    struct PlatformEntry* entry = context;
    struct PlatformBus*   bus = entry->Bus;
    oserr_t               status;

    usched_mtx_lock(&bus->Lock);
    if (bus->Removing) {
        status = OS_EBUSY;
    } else if (request == OSIOCTLREQUEST_DEVICE_RESOURCES) {
        if (buffer == NULL || length != sizeof(struct DeviceResources)) {
            status = OS_EINVALPARAMS;
        } else {
            status = __PlatformResources(entry, buffer);
        }
    } else if (request == OSIOCTLREQUEST_DEVICE_ACTIVATE) {
        if (buffer == NULL || length != sizeof(enum DeviceActivation)) {
            status = OS_EINVALPARAMS;
        } else {
            status = PlatformActivationControl(
                entry,
                principal,
                *(enum DeviceActivation*)buffer
            );
        }
    } else if (request == OSIOCTLREQUEST_INTERRUPT_QUERY) {
        status = PlatformInterruptQuery(entry, buffer, length);
    } else if (request == OSIOCTLREQUEST_INTERRUPT_CONTROL) {
        status = PlatformInterruptControl(entry, principal, buffer, length);
    } else {
        status = OS_ENOTSUPPORTED;
    }
    usched_mtx_unlock(&bus->Lock);
    return status;
}

static const struct DmDeviceProviderOperations g_platformOperations = {
    .PrepareDma = PlatformPrepareDma,
    .ReleaseDma = PlatformReleaseDma,
    .AddReference = __PlatformAddReference,
    .Release = __PlatformRelease,
    .Control = __PlatformControl
};

oserr_t
PlatformBusCreate(
    _In_  struct FdtTree*      tree,
    _Out_ struct PlatformBus** busOut)
{
    struct PlatformBus* bus;
    oserr_t             status;

    if (tree == NULL || busOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    *busOut = NULL;
    
    bus = calloc(1, sizeof(struct PlatformBus));
    if (bus == NULL) {
        return OS_EOOM;
    }

    atomic_init(&bus->References, 0);
    usched_mtx_init(&bus->Lock, USCHED_MUTEX_PLAIN);
    FdtTreeAddReference(tree);
    bus->Tree = tree;
    
    status = PlatformCollectDevices(bus, tree);
    if (status != OS_EOK) {
        // Collection runs before publication or I/O handle creation, so the
        // partially built device list can be destroyed here.
        PlatformBusDestroy(bus);
        return status;
    }
    
    *busOut = bus;
    return OS_EOK;
}

static oserr_t
__PublishRoot(
    _InOut_ struct PlatformBus* bus)
{
    struct DmDeviceRegistration registration = { .Kind = DmDeviceDescriptionGeneric };
    Device_t*                   device;
    oserr_t                     status;

    device = calloc(1, sizeof(Device_t));
    if (device == NULL) {
        return OS_EOOM;
    }

    device->Length = sizeof(Device_t);
    device->Identification.Description = strdup("Device Tree platform");
    if (device->Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    registration.Description = device;
    
    status = DmPublicationAdd(&bus->Publication, &registration, 0, &bus->DeviceId);
    if (status != OS_EOK) {
        free(device->Identification.Description);
        free(device);
    }
    return status;
}

static oserr_t
__PublishEntry(
    _InOut_ struct PlatformBus*   bus,
    _InOut_ struct PlatformEntry* entry)
{
    struct DmDeviceRegistration registration = { .Kind = DmDeviceDescriptionPlatform };
    struct PlatformEntry*       parent;
    PlatformDevice_t*           device;
    uuid_t                      parentId = bus->DeviceId;
    oserr_t                     status;

    if (entry->ParentNode != UINT32_MAX) {
        for (parent = bus->Entries; parent != NULL; parent = parent->Next) {
            if (parent->Firmware.FirmwareNode == entry->ParentNode) {
                break;
            }
        }

        if (parent == NULL || parent->DeviceId == UUID_INVALID) {
            return OS_EINVALPARAMS;
        }
        
        parentId = parent->DeviceId;
    }

    device = malloc(sizeof(PlatformDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }

    *device = entry->Firmware;
    
    device->Base.ParentId = parentId;
    device->Base.Identification.Description = strdup(entry->Firmware.Base.Identification.Description);
    if (device->Base.Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    
    registration.Description = &device->Base;
    if (!entry->IsBus) {
        registration.Provider.Operations = &g_platformOperations;
        registration.Provider.Context = entry;
    }
    
    status = DmPublicationAdd(
        &bus->Publication,
        &registration,
        !entry->IsBus,
        &entry->DeviceId
    );
    if (status != OS_EOK) {
        free(device->Base.Identification.Description);
        free(device);
    }
    return status;
}

oserr_t
PlatformBusPublish(
    _InOut_ struct PlatformBus* bus)
{
    struct PlatformEntry* entry;
    oserr_t               status;
    oserr_t               cleanup;

    if (bus->Removing) {
        return OS_EBUSY;
    }
    
    if (bus->Publication.State == DmPublicationRemoving) {
        status = DmPublicationRemove(&bus->Publication);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (bus->Publication.State == DmPublicationCollecting) {
        status = __PublishRoot(bus);
        if (status != OS_EOK) {
            goto rollback;
        }
        
        for (entry = bus->Entries; entry != NULL; entry = entry->Next) {
            status = __PublishEntry(bus, entry);
            if (status != OS_EOK) {
                goto rollback;
            }
        }
        
        status = DmPublicationFinish(&bus->Publication);
        if (status != OS_EOK) {
            goto rollback;
        }
    }

    return DmPublicationEnableBinding(&bus->Publication);

rollback:
    cleanup = DmPublicationRemove(&bus->Publication);
    if (cleanup != OS_EOK) {
        ERROR("PlatformBusPublish: rollback kept inventory (%u)", cleanup);
    }
    return status;
}

oserr_t
PlatformBusDestroy(
    _In_ struct PlatformBus* bus)
{
    struct PlatformEntry* entry;
    struct PlatformEntry* next;
    oserr_t               status;

    if (bus == NULL) {
        return OS_EOK;
    }

    usched_mtx_lock(&bus->Lock);
    
    // Registered dependencies and prepared devices can still use shared hardware
    // when no registry request is running. Clients must release interrupts and
    // stop their devices first. Keep requests available after OS_EBUSY so clients
    // can finish that cleanup.
    if (bus->Dependencies != NULL) {
        usched_mtx_unlock(&bus->Lock);
        return OS_EBUSY;
    }

    for (entry = bus->Entries; entry != NULL; entry = entry->Next) {
        if (PlatformIsRuntimeBusy(entry)) {
            usched_mtx_unlock(&bus->Lock);
            return OS_EBUSY;
        }
    }
    bus->Removing = 1;
    usched_mtx_unlock(&bus->Lock);
    
    status = DmPublicationRemove(&bus->Publication);
    if (status != OS_EOK) {
        return status;
    }
    
    if (atomic_load(&bus->References) != 0) {
        return OS_EBUSY;
    }
    
    for (entry = bus->Entries; entry != NULL; entry = entry->Next) {
        status = DmIoDestroy(entry->Resources.Registers, entry->Firmware.RegisterCount);
        if (status != OS_EOK) {
            return status;
        }
    }

    for (entry = bus->Entries; entry != NULL; entry = next) {
        next = entry->Next;
        free(entry->Firmware.Base.Identification.Description);
        free(entry);
    }
    
    FdtTreeRelease(bus->Tree);
    free(bus);
    return OS_EOK;
}
