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

    oserr = MemorySpaceCopyUser(&deviceInterrupt->Line, &request.Line, sizeof(request.Line), true);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    oserr = MemorySpaceCopyUser(&deviceInterrupt->MsiAddress, &request.MsiAddress, sizeof(request.MsiAddress), true);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser(&deviceInterrupt->MsiValue, &request.MsiValue, sizeof(request.MsiValue), true);
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
        (flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT))) {
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
