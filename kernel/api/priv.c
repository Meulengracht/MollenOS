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
#include <ddk/types/interrupt.h>
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
__CopyAndVerifyAllocationRequest(
    _In_ InterruptAllocationRequest_t* userRequest,
    _In_ InterruptAllocationRequest_t* kernelRequest)
{
    oserr_t status;

    status = MemorySpaceCopyUser(
        userRequest,
        kernelRequest,
        sizeof(InterruptAllocationRequest_t),
        false
    );
    if (status != OS_EOK) {
        return status;
    }
    
    if (kernelRequest->Minimum == 0 || kernelRequest->Minimum > kernelRequest->Optimal) {
        return OS_EINVALPARAMS;
    }
    if (kernelRequest->Strategies == 0 || (kernelRequest->Strategies & ~INTERRUPT_STRATEGY_ANY)) {
        return OS_EINVALPARAMS;
    }
    if (kernelRequest->Flags & ~INTERRUPT_EXCLUSIVE) {
        return OS_EINVALPARAMS;
    }

    // Currently we only support INTx delivery, and only 1 interrupt.
    if (!(kernelRequest->Strategies & INTERRUPT_STRATEGY_INTx) || kernelRequest->Minimum != 1) {
        return OS_ENOTSUPPORTED;
    }
    if (kernelRequest->Line.Line < 0) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

oserr_t
ScInterruptAllocate(
    _In_  InterruptAllocationRequest_t* userRequest,
    _Out_ InterruptAllocationResult_t*  userResult)
{
    InterruptAllocationRequest_t request;
    InterruptAllocationResult_t  result;
    InterruptDescriptor_t        descriptor = { 0 };
    oserr_t                      status;

    if (userRequest == NULL || userResult == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = __CopyAndVerifyAllocationRequest(userRequest, &request);
    if (status != OS_EOK) {
        return status;
    }
    
    descriptor.Line = request.Line.Line;
    descriptor.Pin = request.Line.Pin;
    descriptor.AcpiConform = request.Line.AcpiConform;
    
    // perform the reservation
    status = InterruptReserve(&descriptor, request.Flags, &result.Handle);
    if (status != OS_EOK) {
        return status;
    }

    // currently just support 1 irq and intx
    result.Count = 1;
    result.Strategy = INTERRUPT_STRATEGY_INTx;

    status = MemorySpaceCopyUser(
        userResult,
        &result,
        sizeof(InterruptAllocationResult_t),
        true
    );
    if (status != OS_EOK) {
        // Just unregister if it fails, no resources has been bound to
        // the irq yet
        InterruptUnregister(result.Handle);
    }
    return status;
}

static oserr_t
__CheckInterruptOwner(
    _In_ uuid_t handle)
{
    SystemInterrupt_t* entry;
    int                hasAccess;

    entry = InterruptGet(handle);
    if (entry == NULL) {
        return OS_ENOENT;
    }

    // For a process/driver to own an interrupt it must not be a
    // kernel interrupt, and the entry must belong to the same ms handle.
    hasAccess = !(entry->Flags & INTERRUPT_KERNEL) &&
            entry->Owner == GetCurrentMemorySpaceHandle();
    
    InterruptPut(entry);
    return hasAccess ? OS_EOK : OS_EPERMISSIONS;
}

oserr_t
ScInterruptBind(
    _In_ uuid_t                  handle,
    _In_ InterruptBindRequest_t* userRequest)
{
    InterruptBindRequest_t request;
    oserr_t                status;

    // Before binding resources to an interrupt handle
    // ensure we can do this
    status = __CheckInterruptOwner(handle);
    if (status != OS_EOK) {
        return status;
    }
    if (userRequest == NULL) {
        return OS_EINVALPARAMS;
    }
    
    // get the request data
    status = MemorySpaceCopyUser(
        userRequest,
        &request,
        sizeof(InterruptBindRequest_t),
        false
    );
    if (status != OS_EOK) {
        return status;
    }
    if (request.Index != 0) {
        return OS_EINVALPARAMS;
    }

    // execute the request
    return InterruptBind(handle, &request.Resources);
}

oserr_t
ScInterruptEnable(
    _In_ uuid_t handle)
{
    oserr_t status;

    // Ownership check before enabling an interrupt
    status = __CheckInterruptOwner(handle);
    if (status != OS_EOK) {
        return status;
    }
    return InterruptSetEnabled(handle, 1);
}

oserr_t
ScInterruptDisable(
    _In_ uuid_t handle)
{
    oserr_t status;

    // Ownership check before disabling an interrupt
    status = __CheckInterruptOwner(handle);
    if (status != OS_EOK) {
        return status;
    }
    return InterruptSetEnabled(handle, 0);
}

oserr_t
ScInterruptUnbind(
    _In_ uuid_t handle)
{
    oserr_t status;

    // Ownership check before we release resources asssociated
    // with the interrupt
    status = __CheckInterruptOwner(handle);
    if (status != OS_EOK) {
        return status;
    }
    return InterruptUnbind(handle);
}

oserr_t
ScInterruptRelease(
    _In_ uuid_t handle)
{
    oserr_t status;

    // Ownership check before we release the irq reservation
    status = __CheckInterruptOwner(handle);
    if (status != OS_EOK) {
        return status;
    }
    return InterruptUnregister(handle);
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
