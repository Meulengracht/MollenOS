/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * System Calls
 */

#define __MODULE "SCIF"
//#define __TRACE

#include <acpiinterface.h>
#include <arch/interrupts.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <ddk/acpi.h>
#include <deviceio.h>
#include <firmware.h>
#include <handle.h>
#include <heap.h>
#include <interrupts.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>

typedef struct __InterruptSet {
    uuid_t   Owner;
    uint32_t Count;
    unsigned int Flags;
    uuid_t   DeviceId;
    uint32_t Segment;
    uint8_t  Bus;
    uint8_t  Slot;
    uint8_t  Function;
    int      MsiCommitted;
    uint32_t RouteCount;
    InterruptMsiRoute_t Routes[INTERRUPT_MAXVECTORS];
    uuid_t   Sources[INTERRUPT_MAXVECTORS];
} __InterruptSet_t;

static oserr_t
__QueueInterruptSetQuiesce(
    _In_ __InterruptSet_t* set,
    _In_ const InterruptMsiRoute_t* routes,
    _In_ uint32_t          count)
{
    DeviceInterruptQuiesceRequest_t request = { 0 };
    uuid_t                         token;

    if (!(set->Flags & INTERRUPT_MSI)) {
        return OS_EOK;
    }
    if (count == 0) {
        return OS_EOK;
    }
    request.DeviceId = set->DeviceId;
    request.Segment = set->Segment;
    request.Bus = set->Bus;
    request.Slot = set->Slot;
    request.Function = set->Function;
    return InterruptMsiQuiesceEnqueue(
        &request,
        set->Owner,
        routes,
        count,
        &token
    );
}

static void
__DestroyInterruptSet(
    _In_ void* resource)
{
    __InterruptSet_t*   set = (__InterruptSet_t*)resource;
    InterruptMsiRoute_t routes[INTERRUPT_MAXVECTORS];
    uint32_t            routeCount = 0;

    for (uint32_t i = 0; i < set->Count; i++) {
        if (set->Sources[i] != UUID_INVALID) {
            (void)InterruptUnregisterOwned(set->Sources[i], set->Owner);
        }
    }
    if (set->Flags & INTERRUPT_MSI) {
        for (uint32_t i = 0; i < set->RouteCount; i++) {
            if (set->Routes[i].ControllerId != 0) {
                routes[routeCount++] = set->Routes[i];
            }
        }
        if (set->MsiCommitted) {
            (void)__QueueInterruptSetQuiesce(set, routes, routeCount);
        } else {
            for (uint32_t i = 0; i < routeCount; i++) {
                if (InterruptMsiReleaseTableRoute(&routes[i]) == OS_EOK) {
                    PlatformMsiRelease(&routes[i]);
                }
            }
        }
    }
    kfree(set);
}

oserr_t
ScFirmwareQuery(
        _Out_ OSFirmwareInfo_t* infoOut)
{
    OSFirmwareInfo_t info;
    oserr_t          oserr;

    oserr = FirmwareQuery(&info);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return MemorySpaceCopyUser(infoOut, &info, sizeof(info), true);
}

static oserr_t
__LocateFirmwareTable(
        _In_  const OSFirmwareTableKey_t* userKey,
        _Out_ const void**                dataOut,
        _Out_ OSFirmwareTable_t*          tableOut)
{
    OSFirmwareTableKey_t key;
    oserr_t              oserr;

    oserr = MemorySpaceCopyUser((void*)userKey, &key, sizeof(key), false);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return FirmwareLocate(&key, dataOut, tableOut);
}

oserr_t
ScFirmwareTableLocate(
        _In_  const OSFirmwareTableKey_t* key,
        _Out_ OSFirmwareTable_t*          tableOut)
{
    OSFirmwareTable_t table;
    const void*       data;
    oserr_t           oserr;

    if (tableOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __LocateFirmwareTable(key, &data, &table);
    if (oserr == OS_EOK) {
        oserr = MemorySpaceCopyUser(tableOut, &table, sizeof(table), true);
    }
    return oserr;
}

oserr_t
ScFirmwareTableRead(
        _In_  const OSFirmwareTableKey_t* key,
        _In_  void*                       buffer,
        _In_  size_t                      size,
        _Out_ size_t*                     lengthOut)
{
    OSFirmwareTable_t table;
    const void*       data;
    oserr_t           oserr;

    if (lengthOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __LocateFirmwareTable(key, &data, &table);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser(lengthOut, &table.Length, sizeof(table.Length), true);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (buffer == NULL || size < table.Length) {
        return OS_EBUFFER;
    }
    return MemorySpaceCopyUser(buffer, (void*)data, table.Length, true);
}

oserr_t
ScFirmwareTableMap(
        _In_  const OSFirmwareTableKey_t* key,
        _Out_ const void**                mappingOut,
        _Out_ size_t*                     lengthOut)
{
    MemorySpace_t*    memorySpace = GetCurrentMemorySpace();
    OSFirmwareTable_t table;
    const void*       data;
    paddr_t*          pages;
    vaddr_t           mapping;
    unsigned int      previousAttributes;
    oserr_t           oserr;

    if (mappingOut == NULL || lengthOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __LocateFirmwareTable(key, &data, &table);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Firmware data shares pages with unrelated kernel or firmware memory, so the
    // caller receives private zeroed pages holding a copy rather than an alias.
    pages = kmalloc(sizeof(paddr_t) * DIVUP(table.Length, GetMemorySpacePageSize()));
    if (pages == NULL) {
        return OS_EOOM;
    }

    oserr = MemorySpaceMap(
            memorySpace,
            &(struct MemorySpaceMapOptions) {
                .Pages = pages,
                .Length = table.Length,
                .Mask = __MASK,
                .Flags = MAPPING_USERSPACE | MAPPING_COMMIT | MAPPING_CLEAN,
                .PlacementFlags = MAPPING_VIRTUAL_PROCESS
            },
            &mapping
    );
    kfree(pages);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser((void*)mapping, (void*)data, table.Length, true);
    if (oserr != OS_EOK) {
        (void)MemorySpaceUnmap(memorySpace, mapping, table.Length);
        return oserr;
    }
    
    oserr = MemorySpaceChangeProtection(
        memorySpace, mapping, table.Length,
        MAPPING_USERSPACE | MAPPING_COMMIT | MAPPING_READONLY,
        &previousAttributes
    );
    if (oserr != OS_EOK) {
        (void)MemorySpaceUnmap(memorySpace, mapping, table.Length);
        return oserr;
    }

    oserr = MemorySpaceCopyUser(lengthOut, &table.Length, sizeof(table.Length), true);
    if (oserr == OS_EOK) {
        oserr = MemorySpaceCopyUser(mappingOut, &mapping, sizeof(mapping), true);
    }
    if (oserr != OS_EOK) {
        (void)MemorySpaceUnmap(memorySpace, mapping, table.Length);
    }
    return oserr;
}

oserr_t
ScAcpiQueryInterrupt(
    _In_  int           bus,
    _In_  int           device,
    _In_  int           pin,
    _Out_ int*          interruptOut,
    _Out_ unsigned int* acpiConformOut)
{
#ifdef __OSCONFIG_ACPI_SUPPORT
    return AcpiDeviceGetInterrupt(bus, device, pin, interruptOut, acpiConformOut);
#else
    (void)bus;
    (void)device;
    (void)pin;
    (void)interruptOut;
    (void)acpiConformOut;
    return OS_ENOTSUPPORTED;
#endif
}

oserr_t
ScIoSpaceRegister(
    _In_ DeviceIo_t* ioSpace)
{
    if (ioSpace == NULL) {
        return OS_EUNKNOWN;
    }
    return RegisterSystemDeviceIo(ioSpace);
}

oserr_t
ScIoSpaceAcquire(
    _In_ DeviceIo_t* IoSpace)
{
    if (IoSpace == NULL) {
        return OS_EUNKNOWN;
    }
    return AcquireSystemDeviceIo(IoSpace);
}

oserr_t
ScIoSpaceRelease(
    _In_ DeviceIo_t* ioSpace)
{
    if (ioSpace == NULL) {
        return OS_EUNKNOWN;
    }
    return ReleaseSystemDeviceIo(ioSpace);
}

oserr_t
ScIoSpaceDestroy(
    _In_ DeviceIo_t* ioSpace)
{
    if (ioSpace == NULL) {
        return OS_EINVALPARAMS;
    }
    DestroyHandle(ioSpace->Id);
    return OS_EOK;
}

static oserr_t
__CopyInterruptResults(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ DeviceInterrupt_t* request
)
{
    oserr_t oserr;

    oserr = MemorySpaceCopyUser(&deviceInterrupt->Line, &request->Line, sizeof(request->Line), true);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    oserr = MemorySpaceCopyUser(&deviceInterrupt->MsiAddress, &request->MsiAddress, sizeof(request->MsiAddress), true);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser(&deviceInterrupt->MsiValue, &request->MsiValue, sizeof(request->MsiValue), true);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return oserr;
}

uuid_t
ScRegisterInterrupt(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ unsigned int       flags)
{
    DeviceInterrupt_t request;
    uuid_t            id;
    oserr_t           oserr;

    if (deviceInterrupt == NULL ||
        (flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT | INTERRUPT_MSI))) {
        return UUID_INVALID;
    }

    // Do not trust the user-space memory here; copy it into kernel space first.
    oserr = MemorySpaceCopyUser(
        deviceInterrupt,
        &request,
        sizeof(request),
        false
    );
    if (oserr != OS_EOK) {
        return UUID_INVALID;
    }

    // Now we register the interrupt
    id = InterruptRegister(&request, flags);
    if (id == UUID_INVALID) {
        return UUID_INVALID;
    }

    // Only the resolved outputs are written back, never the full request.
    oserr = __CopyInterruptResults(deviceInterrupt, &request);
    if (oserr != OS_EOK) {
        (void)InterruptUnregister(id);
        return UUID_INVALID;
    }
    return id;
}

oserr_t
ScUnregisterInterrupt(
        _In_ uuid_t sourceId)
{
    return InterruptUnregister(sourceId);
}

oserr_t
ScRegisterInterruptSet(
    _In_  DeviceInterrupt_t* interrupts,
    _In_  uint32_t           count,
    _In_  unsigned int       flags,
    _Out_ uuid_t*             setOut)
{
    DeviceInterrupt_t* descriptors;
    __InterruptSet_t*  set;
    uuid_t             setId;
    oserr_t            oserr;

    if (interrupts == NULL || setOut == NULL || count == 0 || count > INTERRUPT_MAXVECTORS ||
        (flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT))) {
        return OS_EINVALPARAMS;
    }

    descriptors = (DeviceInterrupt_t*)kmalloc(sizeof(DeviceInterrupt_t) * count);
    set = (__InterruptSet_t*)kmalloc(sizeof(__InterruptSet_t));
    if (descriptors == NULL || set == NULL) {
        if (descriptors != NULL) {
            kfree(descriptors);
        }
        if (set != NULL) {
            kfree(set);
        }
        return OS_EOOM;
    }

    oserr = MemorySpaceCopyUser(
        interrupts,
        descriptors,
        sizeof(DeviceInterrupt_t) * count,
        false
    );
    if (oserr != OS_EOK) {
        kfree(descriptors);
        kfree(set);
        return oserr;
    }

    if (flags & INTERRUPT_MSI) {
        DeviceInterrupt_t* first = &descriptors[0];
        if (!first->IsPci || first->DeviceId == UUID_INVALID ||
            first->Slot > 31 || first->Function > 7 ||
            first->Bus > UINT8_MAX) {
            kfree(descriptors);
            kfree(set);
            return OS_EINVALPARAMS;
        }
        for (uint32_t i = 1; i < count; i++) {
            if (!descriptors[i].IsPci || descriptors[i].DeviceId != first->DeviceId ||
                descriptors[i].Segment != first->Segment || descriptors[i].Bus != first->Bus ||
                descriptors[i].Slot != first->Slot || descriptors[i].Function != first->Function) {
                kfree(descriptors);
                kfree(set);
                return OS_EINVALPARAMS;
            }
        }
    }

    memset(set, 0, sizeof(__InterruptSet_t));
    set->Owner = GetCurrentMemorySpaceHandle();
    set->Flags = flags;
    if (flags & INTERRUPT_MSI) {
        set->DeviceId = descriptors[0].DeviceId;
        set->Segment = descriptors[0].Segment;
        set->Bus = (uint8_t)descriptors[0].Bus;
        set->Slot = (uint8_t)descriptors[0].Slot;
        set->Function = (uint8_t)descriptors[0].Function;
    }
    for (uint32_t i = 0; i < count; i++) {
        set->Sources[i] = UUID_INVALID;
    }

    if (flags & INTERRUPT_MSI) {
        for (uint32_t i = 0; i < count; i++) {
            oserr = PlatformMsiAllocate(&descriptors[i]);
            if (oserr != OS_EOK) {
                goto cleanup;
            }
            set->Routes[i].ControllerId = descriptors[i].MsiControllerId;
            set->Routes[i].HwIrq = descriptors[i].MsiHwIrq;
            set->Routes[i].Index = descriptors[i].MsiIndex;
            set->Routes[i].ParentLine = descriptors[i].MsiParentLine;
            set->Routes[i].Flags = descriptors[i].MsiRouteFlags;
            set->RouteCount++;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        set->Sources[i] = InterruptRegister(&descriptors[i], flags);
        if (set->Sources[i] == UUID_INVALID) {
            oserr = OS_EUNKNOWN;
            goto cleanup;
        }
        set->Count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        oserr = __CopyInterruptResults(&interrupts[i], &descriptors[i]);
        if (oserr != OS_EOK) {
            goto cleanup;
        }
    }

    setId = CreateHandle(HandleTypeInterruptSet, __DestroyInterruptSet, set);
    if (setId == UUID_INVALID) {
        oserr = OS_EOOM;
        goto cleanup;
    }

    if (flags & INTERRUPT_MSI) {
        oserr = InterruptMsiCommitRoutes(set->Sources, set->Count);
        if (oserr != OS_EOK) {
            (void)DestroyHandle(setId);
            kfree(descriptors);
            return oserr;
        }
        set->MsiCommitted = 1;
    }

    oserr = MemorySpaceCopyUser(setOut, &setId, sizeof(setId), true);
    if (oserr != OS_EOK) {
        for (uint32_t i = 0; i < set->Count; i++) {
            (void)InterruptUnregisterOwned(set->Sources[i], set->Owner);
            set->Sources[i] = UUID_INVALID;
        }
        if (set->MsiCommitted) {
            (void)__QueueInterruptSetQuiesce(set, set->Routes, set->RouteCount);
        }
        set->Count = 0;
        set->RouteCount = 0;
        (void)DestroyHandle(setId);
    }
    kfree(descriptors);
    return oserr;

cleanup:
    for (uint32_t i = 0; i < set->Count; i++) {
        (void)InterruptUnregisterOwned(set->Sources[i], set->Owner);
    }
    for (uint32_t i = 0; i < set->RouteCount; i++) {
        if (InterruptMsiReleaseTableRoute(&set->Routes[i]) == OS_EOK) {
            PlatformMsiRelease(&set->Routes[i]);
        }
    }
    kfree(descriptors);
    kfree(set);
    return oserr;
}

oserr_t
ScDestroyInterruptSet(
    _In_ uuid_t setId)
{
    __InterruptSet_t*   set;
    InterruptMsiRoute_t routes[INTERRUPT_MAXVECTORS];
    uint32_t            routeCount = 0;
    oserr_t             oserr;

    oserr = AcquireHandleOfType(setId, HandleTypeInterruptSet, (void**)&set);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (set->Owner != GetCurrentMemorySpaceHandle()) {
        (void)DestroyHandle(setId);
        return OS_EPERMISSIONS;
    }

    for (uint32_t i = 0; i < set->Count; i++) {
        if (set->Sources[i] == UUID_INVALID) {
            continue;
        }
        oserr = InterruptUnregisterOwned(set->Sources[i], set->Owner);
        if (oserr != OS_EOK) {
            if (set->MsiCommitted) {
                (void)__QueueInterruptSetQuiesce(set, routes, routeCount);
            }
            (void)DestroyHandle(setId);
            return oserr;
        }
        if (set->Flags & INTERRUPT_MSI) {
            routes[routeCount++] = set->Routes[i];
            set->Routes[i].ControllerId = 0;
        }
        set->Sources[i] = UUID_INVALID;
    }

    oserr = set->MsiCommitted
            ? __QueueInterruptSetQuiesce(set, routes, routeCount) : OS_EOK;
    set->Count = 0;
    set->RouteCount = 0;
    (void)DestroyHandle(setId);
    (void)DestroyHandle(setId);
    return oserr;
}

oserr_t
ScRegisterInterruptQuiesceEvent(
    _In_ uuid_t eventHandle)
{
    return InterruptMsiQuiesceRegister(GetCurrentMemorySpaceHandle(), eventHandle);
}

oserr_t
ScGetInterruptQuiesceRequest(
    _Out_ DeviceInterruptQuiesceRequest_t* requestOut)
{
    DeviceInterruptQuiesceRequest_t request;
    oserr_t oserr;

    if (requestOut == NULL) {
        return OS_EINVALPARAMS;
    }
    oserr = InterruptMsiQuiesceNext(GetCurrentMemorySpaceHandle(), &request);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return MemorySpaceCopyUser(requestOut, &request, sizeof(request), true);
}

oserr_t
ScCompleteInterruptQuiesce(
    _In_ uuid_t token)
{
    return InterruptMsiQuiesceFinish(GetCurrentMemorySpaceHandle(), token);
}

uuid_t
ScRegisterMsiController(
    _In_ const DeviceMsiControllerDescription_t* description)
{
    DeviceMsiControllerDescription_t request;
    oserr_t                         oserr;

    if (description == NULL) {
        return UUID_INVALID;
    }
    oserr = MemorySpaceCopyUser((void*)description, &request, sizeof(request), false);
    if (oserr != OS_EOK) {
        return UUID_INVALID;
    }
    return InterruptMsiControllerRegister(&request);
}

oserr_t
ScGetProcessBaseAddress(
    _Out_ uintptr_t* baseAddress)
{
    if (baseAddress != NULL) {
        *baseAddress = GetMachine()->MemoryMap.UserCode.Start;
        return OS_EOK;
    }
    return OS_EINVALPARAMS;
}

oserr_t
ScMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* lengthOut)
{
    oserr_t oserr;
    vaddr_t mapping;

    oserr = MemorySpaceCloneMapping(
            GetCurrentMemorySpace(),
            GetCurrentMemorySpace(),
            (vaddr_t)GetMachine()->BootInformation.Ramdisk.Data,
            &mapping,
            GetMachine()->BootInformation.Ramdisk.Length,
            MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_READONLY,
            MAPPING_VIRTUAL_PROCESS
    );
    if (oserr == OS_EOK) {
        *bufferOut = (void*)mapping;
        *lengthOut = GetMachine()->BootInformation.Ramdisk.Length;
    }
    return oserr;
}
