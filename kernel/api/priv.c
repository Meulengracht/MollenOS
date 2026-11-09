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

// This header must appear before the DDK's standalone ACPI subset to
// avoid conflicting platform macros.
#include <acpiinterface.h>
#include <ddk/pci/acpi.h>
#ifdef __OSCONFIG_ACPI_SUPPORT
#include "../acpi/pci.h"
#endif

#include <ddk/types/interrupt-controller.h>
#include <ddk/types/interrupt-group.h>
#include <ddk/types/interrupt.h>
#include <ddk/acpi.h>
#include <string.h>

#include <arch/utils.h>
#include <capabilities.h>
#include <deviceio.h>
#include <firmware.h>
#include <handles/handle.h>
#include <heap.h>
#include <interrupts/interrupts.h>
#include <interrupts/group.h>
#include <interrupts/chain.h>
#include <component/ic.h>
#include <machine.h>
#include <memoryspace.h>

oserr_t
ScCapabilityGrant(
    _In_ Capabilities_t capabilities,
    _In_ uuid_t         memorySpaceHandle)
{
    return CapabilityGrant(capabilities, memorySpaceHandle);
}

oserr_t
ScInterruptMessageControllerResolve(
    _In_  const InterruptMessageControllerIdentity_t* userQuery,
    _Out_ uuid_t*                                  userDomain)
{
    InterruptMessageControllerIdentity_t query;
    uuid_t                               domain;
    oserr_t                              status;

    if (userQuery == NULL || userDomain == NULL) {
        return OS_EINVALPARAMS;
    }

    status = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser((void*)userQuery, &query, sizeof(query), false);
    if (status != OS_EOK) {
        return status;
    }

    status = InterruptMessageControllerResolve(&query, &domain);
    if (status != OS_EOK) {
        return status;
    }

    return MemorySpaceCopyUser(userDomain, &domain, sizeof(domain), true);
}

oserr_t
ScInterruptLineSourceResolve(
    _In_  const InterruptLineSourceIdentity_t* userQuery,
    _Out_ uuid_t*                              userController)
{
    InterruptLineSourceIdentity_t query;
    uuid_t                        controller;
    oserr_t                       status;

    if (userQuery == NULL || userController == NULL) {
        return OS_EINVALPARAMS;
    }

    status = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (status != OS_EOK) {
        return status;
    }

    // Copy the query before validation so userspace cannot change the firmware
    // controller or line while we check it. This call reserves no source.
    status = MemorySpaceCopyUser((void*)userQuery, &query, sizeof(query), false);
    if (status != OS_EOK) {
        return status;
    }

    status = InterruptLineSourceResolve(&query, &controller);
    if (status != OS_EOK) {
        return status;
    }

    // This query allocates no resources. If copying the result fails, the bus
    // can retry the same query without cleanup.
    return MemorySpaceCopyUser(userController, &controller, sizeof(controller), true);
}

oserr_t
ScFirmwareQuery(
    _Out_ OSFirmwareInfo_t* infoOut)
{
    OSFirmwareInfo_t info;
    oserr_t          oserr;

    oserr = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

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

    oserr = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

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

    oserr = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
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

    oserr = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
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

    oserr = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __LocateFirmwareTable(key, &data, &table);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Firmware pages can contain unrelated kernel or firmware data. Copy the
    // table into zeroed pages so the caller can only read the requested data.
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
ScAcpiQueryPci(
    _In_  const struct AcpiPciQuery* query,
    _Out_ union AcpiPciResult*       result)
{
    struct AcpiPciQuery request;
    union AcpiPciResult response = { 0 };
    oserr_t             status;

    status = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (status != OS_EOK) {
        return status;
    }

    // Copy the request once before running any AML. Userspace can change the
    // memory at any time, and the validation and namespace lookups must all see
    // the same request.
    status = MemorySpaceCopyUser(&request, (void*)query, sizeof(request), false);
    if (status != OS_EOK) {
        return status;
    }

#ifdef __OSCONFIG_ACPI_SUPPORT
    status = AcpiPciQuery(&request, &response);
    if (status == OS_EOK) {
        // The union was zeroed, so unused bytes never leak kernel stack data.
        status = MemorySpaceCopyUser(
            result,
            &response,
            sizeof(union AcpiPciResult),
            true
        );
    }
    return status;
#else
    (void)result;
    (void)response;
    return OS_ENOTSUPPORTED;
#endif
}

oserr_t
ScIoSpaceRegister(
    _In_ DeviceIo_t* ioSpace)
{
    oserr_t oserr;

    if (ioSpace == NULL) {
        return OS_EUNKNOWN;
    }

    oserr = CapabilityVerify(CAPABILITY_DEVICEIO_MANAGEMENT);
    if (oserr != OS_EOK) {
        return oserr;
    }

    return RegisterSystemDeviceIo(ioSpace);
}

oserr_t
ScIoSpaceAcquire(
    _In_ DeviceIo_t* IoSpace)
{
    oserr_t oserr;

    if (IoSpace == NULL) {
        return OS_EUNKNOWN;
    }

    oserr = CapabilityVerify(CAPABILITY_DEVICEIO_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

    return AcquireSystemDeviceIo(IoSpace);
}

oserr_t
ScIoSpaceRelease(
    _In_ DeviceIo_t* ioSpace)
{
    oserr_t oserr;

    if (ioSpace == NULL) {
        return OS_EUNKNOWN;
    }

    oserr = CapabilityVerify(CAPABILITY_DEVICEIO_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

    return ReleaseSystemDeviceIo(ioSpace);
}

oserr_t
ScIoSpaceDestroy(
    _In_ DeviceIo_t* ioSpace)
{
    oserr_t oserr;

    if (ioSpace == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = CapabilityVerify(CAPABILITY_DEVICEIO_MANAGEMENT);
    if (oserr != OS_EOK) {
        return oserr;
    }

    DestroyHandle(ioSpace->Id);
    return OS_EOK;
}

oserr_t
ScIoSpaceMap(
    _In_  uuid_t     id,
    _In_  size_t     offset,
    _In_  size_t     length,
    _Out_ uintptr_t* userMapping)
{
    uintptr_t mapping;
    size_t    pageOffset;
    oserr_t   oserr;

    oserr = CapabilityVerify(CAPABILITY_DEVICEIO_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (userMapping == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = MapSystemDeviceIo(id, offset, length, &mapping);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser(userMapping, &mapping, sizeof(mapping), true);
    if (oserr != OS_EOK) {
        pageOffset = mapping % GetMemorySpacePageSize();
        MemorySpaceUnmap(GetCurrentMemorySpace(), mapping - pageOffset, length + pageOffset);
    }
    return oserr;
}

static oserr_t
__CopyAndVerifyGroupRequest(
    _In_ InterruptGroupRequest_t* userRequest,
    _In_ InterruptGroupRequest_t* kernelRequest)
{
    oserr_t status;

    status = MemorySpaceCopyUser(
        userRequest,
        kernelRequest,
        sizeof(InterruptGroupRequest_t),
        false
    );
    if (status != OS_EOK) {
        return status;
    }

    if (kernelRequest->Minimum == 0 || kernelRequest->Minimum > kernelRequest->Optimal) {
        return OS_EINVALPARAMS;
    }
    if (kernelRequest->Kind < InterruptSourceLine || kernelRequest->Kind > InterruptSourceMessage) {
        return OS_EINVALPARAMS;
    }
    if (kernelRequest->Flags & ~INTERRUPT_EXCLUSIVE) {
        return OS_EINVALPARAMS;
    }

    // The caller has already selected the resource kind. The kernel must still
    // check the source and device access before allocating interrupts.
    if (kernelRequest->Kind != InterruptSourceMessage && kernelRequest->Line.Line < 0) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

oserr_t
ScInterruptAllocate(
    _In_  InterruptGroupRequest_t* userRequest,
    _Out_ InterruptGroupResult_t*  userResult)
{
    InterruptGroupRequest_t request;
    InterruptGroupResult_t  result;
    oserr_t                 oserr;

    if (userRequest == NULL || userResult == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __CopyAndVerifyGroupRequest(userRequest, &request);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = InterruptGroupCreate(&request, 0, &result);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser(
        userResult,
        &result,
        sizeof(InterruptGroupResult_t),
        true
    );
    if (oserr != OS_EOK) {
        // No resources have been bound yet, so we can release the group here.
        InterruptGroupRelease(result.Handle);
    }
    return oserr;
}

static oserr_t
__CheckInterruptOwner(
    _In_ uuid_t handle)
{
    return InterruptGroupCheckOwner(handle);
}

oserr_t
ScInterruptGetMessage(
    _In_  uuid_t                    handle,
    _In_  uint32_t                  index,
    _Out_ DeviceInterruptMessage_t* userMessage)
{
    InterruptMessage_t       message;
    DeviceInterruptMessage_t result;
    oserr_t                  oserr;

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (userMessage == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = InterruptGroupGetMessage(handle, index, &message);
    if (oserr != OS_EOK) {
        return oserr;
    }

    result.AddressLow = (uint32_t)message.Address;
    result.AddressHigh = (uint32_t)(message.Address >> 32U);
    result.Data = message.Data;
    return MemorySpaceCopyUser(userMessage, &result, sizeof(result), true);
}

oserr_t
ScInterruptBind(
    _In_ uuid_t                  handle,
    _In_ InterruptBindRequest_t* userRequest)
{
    InterruptBindRequest_t request;
    oserr_t                oserr;

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The interrupt group must belong to the caller before we bind resources.
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (userRequest == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = MemorySpaceCopyUser(
        userRequest,
        &request,
        sizeof(InterruptBindRequest_t),
        false
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    return InterruptGroupBind(handle, request.Index, &request.Resources);
}

oserr_t
ScInterruptEnable(
    _In_ uuid_t handle)
{
    oserr_t oserr;

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The interrupt group must belong to the caller.
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return InterruptGroupSetEnabled(handle, 1);
}

oserr_t
ScInterruptDisable(
    _In_ uuid_t handle)
{
    oserr_t oserr;

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The interrupt group must belong to the caller.
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    // Stop dispatch before unmapping resources. Keep both steps in one group
    // update so the caller can retry the disable call if cleanup fails.
    return InterruptGroupDisable(handle);
}

oserr_t
ScInterruptRelease(
    _In_ uuid_t handle)
{
    oserr_t oserr;

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The interrupt group must belong to the caller before we release it.
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return InterruptGroupRelease(handle);
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
ScInterruptReap(
    _In_ uuid_t handle)
{
    oserr_t status;

    // Cleaning up another process's group requires bus-management and interrupt
    // permissions. The core also checks that the process has exited.
    status = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (status != OS_EOK) {
        return status;
    }

    status = CapabilityVerify(CAPABILITY_DEVICEIO_MANAGEMENT);
    if (status != OS_EOK) {
        return status;
    }
    return InterruptGroupReap(handle);
}

oserr_t
ScMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* lengthOut)
{
    oserr_t oserr;
    vaddr_t mapping;

    oserr = CapabilityVerify(CAPABILITY_RAMDISK_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

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

static oserr_t
__VerifyInterruptPrivileges(void)
{
    oserr_t status;

    // Bus handlers run in the kernel, just like device fast handlers. The bus
    // also attaches groups for other processes, so it needs device-management
    // permission and must identify the driver that requested the attachment.
    status = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (status != OS_EOK) {
        return status;
    }
    return CapabilityVerify(CAPABILITY_DEVICEIO_MANAGEMENT);
}

oserr_t
ScInterruptBusHandlerCreate(
    _Out_ uuid_t* userHandle)
{
    uuid_t  handle;
    oserr_t status;

    if (userHandle == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __VerifyInterruptPrivileges();
    if (status != OS_EOK) {
        return status;
    }

    status = InterruptBusHandlerCreate(&handle);
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser(userHandle, &handle, sizeof(uuid_t), true);
    if (status != OS_EOK) {
        // The caller did not receive the handle, so we must release it here.
        // No resources have been mapped yet. Cleanup cannot fail on an unmap.
        InterruptBusHandlerRelease(handle);
    }
    return status;
}

oserr_t
ScInterruptBusHandlerBind(
    _In_ uuid_t                          handle,
    _In_ const InterruptResourceTable_t* userResources)
{
    InterruptResourceTable_t resources;
    DeviceIo_t               io[INTERRUPT_MAX_IO_RESOURCES];
    oserr_t                  status;

    if (userResources == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __VerifyInterruptPrivileges();
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser((void*)userResources, &resources, sizeof(resources), false);
    if (status != OS_EOK) {
        return status;
    }

    // The table contains pointers to userspace I/O descriptions. Copy each
    // description before validation and mapping, as mapping may wait while
    // another bus thread changes the original data.
    for (unsigned int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (resources.IoResources[i] != NULL) {
            status = MemorySpaceCopyUser(resources.IoResources[i], &io[i], sizeof(io[i]), false);
            if (status != OS_EOK) {
                return status;
            }
            resources.IoResources[i] = &io[i];
        }
    }

    return InterruptBusHandlerBind(handle, &resources);
}

oserr_t
ScInterruptBusHandlerRelease(
    _In_ uuid_t handle)
{
    oserr_t status;

    status = __VerifyInterruptPrivileges();
    if (status != OS_EOK) {
        return status;
    }

    // The core checks that this handle belongs to the caller and has no attached
    // groups before unmapping resources. Return errors so the bus can retry.
    return InterruptBusHandlerRelease(handle);
}

oserr_t
ScInterruptBusAttach(
    _In_ const InterruptBusAttachRequest_t* userRequest)
{
    InterruptBusAttachRequest_t request;
    InterruptBusVector_t*       userVectors;
    InterruptBusVector_t*       vectors;
    size_t                      length;
    oserr_t                     status;

    if (userRequest == NULL) {
        return OS_EINVALPARAMS;
    }
    status = __VerifyInterruptPrivileges();
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser((void*)userRequest, &request, sizeof(request), false);
    if (status != OS_EOK) {
        return status;
    }

    if (request.Count == 0 || request.Count > INTERRUPT_BUS_MAX_VECTORS) {
        return OS_EINVALPARAMS;
    }

    if (request.Vectors == NULL) {
        return OS_EINVALPARAMS;
    }

    // Count has been checked above. Copy the entire array before attaching
    // vectors so a failed copy cannot leave the group partly attached.
    length = request.Count * sizeof(InterruptBusVector_t);
    userVectors = request.Vectors;
    vectors = kmalloc(length);
    if (vectors == NULL) {
        return OS_EOOM;
    }

    status = MemorySpaceCopyUser(userVectors, vectors, length, false);
    if (status != OS_EOK) {
        kfree(vectors);
        return status;
    }

    request.Vectors = vectors;
    status = InterruptGroupBusAttach(&request);
    if (status == OS_EOK) {
        // The vectors are attached even if copying the messages back fails.
        // The caller can retry the same request to get the messages without
        // adding another reference to each bus handler.
        status = MemorySpaceCopyUser(userVectors, vectors, length, true);
    }
    // The group stores its own handler references and messages. We can free
    // this temporary array on both success and failure.
    kfree(vectors);
    return status;
}

oserr_t
ScInterruptBusStatus(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    oserr_t status;

    status = __VerifyInterruptPrivileges();
    if (status != OS_EOK) {
        return status;
    }

    // The supplied driver identity must match the process that created the
    // group. The bus calls on that process's behalf.
    return InterruptGroupBusStatus(handle, principal, device);
}

oserr_t
ScInterruptBusClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    oserr_t status;

    status = __VerifyInterruptPrivileges();
    if (status != OS_EOK) {
        return status;
    }

    // The bus must stop the device's interrupt writes before this call.
    // This only records in the kernel that the bus has closed the group.
    return InterruptGroupBusClose(handle, principal, device);
}
