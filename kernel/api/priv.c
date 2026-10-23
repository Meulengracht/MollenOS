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
#include <capabilities.h>
#include <deviceio.h>
#include <firmware.h>
#include <handle.h>
#include <heap.h>
#include <interrupts.h>
#include <interrupts_group.h>
#include <interrupts_chain.h>
#include <component/ic.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>

oserr_t
ScCapabilityGrant(
    _In_ Capabilities_t capabilities,
    _In_ uuid_t         memorySpaceHandle)
{
    return CapabilityGrant(capabilities, memorySpaceHandle);
}

oserr_t
ScInterruptControllerResolve(
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
    oserr_t oserr;

    oserr = CapabilityVerify(CAPABILITY_FIRMWARE_ACCESS);
    if (oserr != OS_EOK) {
        return oserr;
    }

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
    if (kernelRequest->Strategies == 0 || (kernelRequest->Strategies & ~INTERRUPT_STRATEGY_ANY)) {
        return OS_EINVALPARAMS;
    }
    if (kernelRequest->Flags & ~INTERRUPT_EXCLUSIVE) {
        return OS_EINVALPARAMS;
    }

    // libddk checks PCI support before making this request. Vali has no device
    // access checks yet. The group allocator negotiates MSI-X, single MSI or INTx.
    if (kernelRequest->Strategies == INTERRUPT_STRATEGY_INTx && kernelRequest->Line.Line < 0) {
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
        // Just unregister if it fails, no resources has been bound to
        // the irq yet
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

    // Before binding resources to an interrupt handle
    // ensure we can do this
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (userRequest == NULL) {
        return OS_EINVALPARAMS;
    }
    
    // get the request data
    oserr = MemorySpaceCopyUser(
        userRequest,
        &request,
        sizeof(InterruptBindRequest_t),
        false
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    // execute the request
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

    // Ownership check before enabling an interrupt
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

    // Ownership check before disabling an interrupt
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return InterruptGroupSetEnabled(handle, 0);
}

oserr_t
ScInterruptUnbind(
    _In_ uuid_t handle)
{
    oserr_t oserr;

    oserr = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Ownership check before we release resources asssociated
    // with the interrupt
    oserr = __CheckInterruptOwner(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return InterruptGroupUnbind(handle);
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

    // Ownership check before we release the irq reservation
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

    // This operation cleans up another process's group. Require bus-management
    // permission as well as interrupt access; the core also checks owner death.
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
__CheckInterruptProvider(void)
{
    oserr_t status;

    // A completion runs inside the kernel, just like a device's fast handler.
    // A provider also acts on behalf of another process, so interrupt permission
    // alone is insufficient: it must be trusted to manage devices and identify
    // the driver that sent it an attachment request.
    status = CapabilityVerify(CAPABILITY_INTERRUPT_CONTROL);
    if (status != OS_EOK) {
        return status;
    }
    return CapabilityVerify(CAPABILITY_DEVICEIO_MANAGEMENT);
}

oserr_t
ScInterruptCompletionCreate(
    _Out_ uuid_t* userHandle)
{
    uuid_t  handle;
    oserr_t status;

    if (userHandle == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __CheckInterruptProvider();
    if (status != OS_EOK) {
        return status;
    }

    status = InterruptCompletionCreate(&handle);
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser(userHandle, &handle, sizeof(uuid_t), true);
    if (status != OS_EOK) {
        // The caller did not receive its handle, so it cannot request cleanup.
        // Creation maps no resources; we can remove this empty object without
        // needing to unmap anything that could fail and require a later retry.
        InterruptCompletionRelease(handle);
    }
    return status;
}

oserr_t
ScInterruptCompletionBind(
    _In_ uuid_t                          handle,
    _In_ const InterruptResourceTable_t* userResources)
{
    InterruptResourceTable_t resources;
    DeviceIo_t               io[INTERRUPT_MAX_IO_RESOURCES];
    oserr_t                  status;

    if (userResources == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __CheckInterruptProvider();
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser((void*)userResources, &resources, sizeof(resources), false);
    if (status != OS_EOK) {
        return status;
    }

    // Copying the outer table copies pointers, not the I/O descriptions they
    // point to. Copy those descriptions too and replace their pointers with our
    // local copies. Mapping can wait while another provider thread changes its
    // memory; validation and mapping must use the same values throughout.
    for (unsigned int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (resources.IoResources[i] != NULL) {
            status = MemorySpaceCopyUser(resources.IoResources[i], &io[i], sizeof(io[i]), false);
            if (status != OS_EOK) {
                return status;
            }
            resources.IoResources[i] = &io[i];
        }
    }

    return InterruptCompletionBind(handle, &resources);
}

oserr_t
ScInterruptCompletionRelease(
    _In_ uuid_t handle)
{
    oserr_t status;

    status = __CheckInterruptProvider();
    if (status != OS_EOK) {
        return status;
    }

    // The core checks ownership and attached groups before touching mappings.
    // Keep its failure visible so the provider knows to retain and retry the handle.
    return InterruptCompletionRelease(handle);
}

oserr_t
ScInterruptProviderAttach(
    _In_ const struct InterruptProviderAttachRequest* userRequest)
{
    struct InterruptProviderAttachRequest request;
    struct InterruptProviderVector*       userVectors;
    struct InterruptProviderVector*       vectors;
    size_t                                length;
    oserr_t                               status;

    if (userRequest == NULL) {
        return OS_EINVALPARAMS;
    }
    status = __CheckInterruptProvider();
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyUser((void*)userRequest, &request, sizeof(request), false);
    if (status != OS_EOK) {
        return status;
    }

    if (request.Count == 0 || request.Count > INTERRUPT_PROVIDER_MAX_VECTORS) {
        return OS_EINVALPARAMS;
    }

    if (request.Vectors == NULL) {
        return OS_EINVALPARAMS;
    }

    // The count limit above keeps this multiplication and allocation bounded.
    // Copy the whole array before attaching anything, so an unreadable later
    // element cannot leave only the earlier vectors attached.
    length = request.Count * sizeof(struct InterruptProviderVector);
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
    status = InterruptGroupProviderAttach(&request);
    if (status == OS_EOK) {
        // Attachment has already succeeded. Writing the returned messages into
        // user memory can still fail, so the caller may not know the outcome.
        // Keep the attachment: an identical retry returns the same messages
        // without adding another use of each completion.
        status = MemorySpaceCopyUser(userVectors, vectors, length, true);
    }
    // The group keeps completion references and message values, never this
    // temporary array. Free it on both success and failure.
    kfree(vectors);
    return status;
}

oserr_t
ScInterruptProviderCheckEnabled(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    oserr_t status;

    status = __CheckInterruptProvider();
    if (status != OS_EOK) {
        return status;
    }
    // The supplied driver identity must match the group's recorded owner. The
    // provider is a different process, so checking only the caller would be wrong.
    return InterruptGroupProviderCheckEnabled(handle, principal, device);
}

oserr_t
ScInterruptProviderClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    oserr_t status;

    status = __CheckInterruptProvider();
    if (status != OS_EOK) {
        return status;
    }
    // This records closure in the kernel. Stopping the actual device's interrupt
    // writes is the provider's responsibility and must happen before this call.
    return InterruptGroupProviderClose(handle, principal, device);
}
