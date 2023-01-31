/**
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
 */
#define __MODULE        "DVIO"
//#define __TRACE

#include <arch/utils.h>
#include <arch/io.h>
#include <assert.h>
#include <debug.h>
#include <deviceio.h>
#include <ds/list.h>
#include <handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <threading.h>
#include <string.h>

#define VOID_KEY(Key) (void*)(uintptr_t)Key

typedef struct DeviceIOEntry {
    element_t  Header;
    DeviceIo_t Io;
    uuid_t     Owner;
    uintptr_t  MappedAddress;
} DeviceIOEntry_t;

static list_t g_ioSpaces = LIST_INIT;

struct VerifyContext {
    DeviceIo_t* IoSpace;
    int         Valid;
};

static int __DetectIoOverlaps(
    _In_ int        index,
    _In_ element_t* element,
    _In_ void*      context)
{
    DeviceIOEntry_t*      pIoEntry      = element->value;
    struct VerifyContext* verifyContext = context;

    if (verifyContext->IoSpace->Type != pIoEntry->Io.Type) {
        return LIST_ENUMERATE_CONTINUE;
    }

    if (pIoEntry->Io.Type == DeviceIoPortBased) {
        size_t systemIoStart = pIoEntry->Io.Access.Port.Base;
        size_t systemIoLimit = pIoEntry->Io.Access.Port.Base + pIoEntry->Io.Access.Port.Length;
        size_t verifyIoLimit = verifyContext->IoSpace->Access.Port.Base + verifyContext->IoSpace->Access.Port.Length;

        verifyContext->Valid =
                !(ISINRANGE(verifyContext->IoSpace->Access.Port.Base, systemIoStart, systemIoLimit) ||
                        ISINRANGE(verifyIoLimit, systemIoStart, systemIoLimit));

        if (!verifyContext->Valid) {
            WARNING("__DetectIoOverlaps found overlap in io space 0x%" PRIxIN
                            " => 0x%" PRIxIN " [overlap: 0x%" PRIxIN " => 0x%" PRIxIN "]",
                    verifyContext->IoSpace->Access.Port.Base, verifyIoLimit,
                    systemIoStart, systemIoLimit);
        }
    }
    else if (pIoEntry->Io.Type == DeviceIoMemoryBased) {
        size_t systemIoStart = pIoEntry->Io.Access.Memory.PhysicalBase;
        size_t systemIoLimit = pIoEntry->Io.Access.Memory.PhysicalBase + pIoEntry->Io.Access.Memory.Length;
        size_t verifyIoLimit = verifyContext->IoSpace->Access.Memory.PhysicalBase + verifyContext->IoSpace->Access.Memory.Length;

        verifyContext->Valid =
                !(ISINRANGE(verifyContext->IoSpace->Access.Memory.PhysicalBase, systemIoStart, systemIoLimit) ||
                  ISINRANGE(verifyIoLimit, systemIoStart, systemIoLimit));

        if (!verifyContext->Valid) {
            WARNING("__DetectIoOverlaps found overlap in memory space 0x%" PRIxIN
                    " => 0x%" PRIxIN " [overlap: 0x%" PRIxIN " => 0x%" PRIxIN "]",
                    verifyContext->IoSpace->Access.Memory.PhysicalBase, verifyIoLimit,
                    systemIoStart, systemIoLimit);
        }
    }

    if (!verifyContext->Valid) {
        WARNING("__DetectIoOverlaps found overlap in device io space");
        return LIST_ENUMERATE_STOP;
    }
    return LIST_ENUMERATE_CONTINUE;
}

static void
DestroySystemDeviceIo(
    _In_ void* Resource)
{
    DeviceIo_t*       IoSpace = Resource;
    DeviceIOEntry_t* SystemIo;
    
    TRACE("DestroySystemDeviceIo(Id %" PRIuIN ")", IoSpace);

    SystemIo = (DeviceIOEntry_t*)list_find_value(&g_ioSpaces, VOID_KEY(IoSpace->Id));
    if (SystemIo == NULL || SystemIo->Owner != UUID_INVALID) {
        return;
    }
    
    list_remove(&g_ioSpaces, list_find(&g_ioSpaces, VOID_KEY(IoSpace->Id)));
    kfree(SystemIo);
}

oserr_t
RegisterSystemDeviceIo(
    _In_ DeviceIo_t* ioSpace)
{
    DeviceIOEntry_t*     ioEntry;
    struct VerifyContext context = { .IoSpace = ioSpace, .Valid = 1 };
    TRACE("RegisterSystemDeviceIo(ioSpace=0x%" PRIxIN ")", ioSpace);

    if (!ioSpace) {
        return OS_EINVALPARAMS;
    }

    // Before doing anything, we should do a over-lap
    // check before trying to register this
    list_enumerate(&g_ioSpaces, __DetectIoOverlaps, &context);
    if (!context.Valid) {
        return OS_EBUSY;
    }

    // Allocate a new system only copy of the io-space
    // as we don't want anyone to edit our copy
    ioEntry = (DeviceIOEntry_t*)kmalloc(sizeof(DeviceIOEntry_t));
    if (!ioEntry) {
        return OS_EOOM;
    }
    
    memset(ioEntry, 0, sizeof(DeviceIOEntry_t));
    ioEntry->Owner = UUID_INVALID;
    ioSpace->Id     = CreateHandle(HandleTypeGeneric, DestroySystemDeviceIo, ioEntry);
    ELEMENT_INIT(&ioEntry->Header, VOID_KEY(ioSpace->Id), ioEntry);
    memcpy(&ioEntry->Io, ioSpace, sizeof(DeviceIo_t));
    
    return list_append(&g_ioSpaces, &ioEntry->Header);
}

oserr_t
AcquireSystemDeviceIo(
    _In_ DeviceIo_t* devIO)
{
    DeviceIOEntry_t* ioEntry;
    MemorySpace_t*   memorySpace = GetCurrentMemorySpace();
    uuid_t           coreId = ArchGetProcessorCoreId();

    if (devIO == NULL) {
        return OS_EINVALPARAMS;
    }

    TRACE("AcquireSystemDeviceIo(Id %" PRIuIN ")", devIO->Id);

    // Lookup the system copy to validate this requested operation
    ioEntry = (DeviceIOEntry_t*)list_find_value(&g_ioSpaces, VOID_KEY(devIO->Id));

    // Sanitize the system copy
    if (ioEntry == NULL || ioEntry->Owner != UUID_INVALID) {
        ERROR(" > failed to find the requested io-space, id %" PRIuIN "", devIO->Id);
        return OS_EUNKNOWN;
    }
    ioEntry->Owner = GetCurrentMemorySpaceHandle();

    switch (ioEntry->Io.Type) {
        case DeviceIoMemoryBased: {
            vaddr_t mappedAddress;
            paddr_t physicalBase = ioEntry->Io.Access.Memory.PhysicalBase;
            size_t  pageSize    = GetMemorySpacePageSize();
            size_t  length      = ioEntry->Io.Access.Memory.Length + (physicalBase % pageSize);
            oserr_t oserr       = MemorySpaceMap(
                    GetCurrentMemorySpace(),
                    &(struct MemorySpaceMapOptions) {
                        .PhysicalStart = physicalBase,
                        .Length = length,
                        .Flags = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT,
                        .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_PROCESS
                    },
                    &mappedAddress
            );
            if (oserr != OS_EOK) {
                ERROR(" > Failed to allocate memory for device io memory");
                ioEntry->Owner = UUID_INVALID;
                return oserr;
            }

            // Adjust for offset and store in io copies
            mappedAddress                       += physicalBase % pageSize;
            devIO->Access.Memory.VirtualBase   = mappedAddress;
            ioEntry->MappedAddress              = mappedAddress;
            return OS_EOK;
        } break;

        case DeviceIoPortBased: {
            for (size_t i = 0; i < ioEntry->Io.Access.Port.Length; i++) {
                SetDirectIoAccess(coreId, memorySpace, ((uint16_t)(ioEntry->Io.Access.Port.Base + i)), 1);
            }
            return OS_EOK;
        } break;

        default:
            ERROR(" > unimplemented device-io type %" PRIuIN "", ioEntry->Io.Type);
            break;
    }
    ioEntry->Owner = UUID_INVALID;
    return OS_EUNKNOWN;
}

oserr_t
ReleaseSystemDeviceIo(
    _In_ DeviceIo_t* devIO)
{
    DeviceIOEntry_t* ioEntry;
    MemorySpace_t*   memorySpace = GetCurrentMemorySpace();
    uuid_t           coreId = ArchGetProcessorCoreId();
    
    assert(devIO != NULL);
    TRACE("ReleaseSystemDeviceIo(Id %" PRIuIN ")", devIO->Id);

    ioEntry = (DeviceIOEntry_t*)list_find_value(&g_ioSpaces, VOID_KEY(devIO->Id));

    // Sanitize the system copy and do some security checks
    if (ioEntry == NULL || ioEntry->Owner != GetCurrentMemorySpaceHandle()) {
        ERROR(" > failed to find the requested io-space, id %" PRIuIN "", devIO->Id);
        return OS_EUNKNOWN;
    }

    switch (ioEntry->Io.Type) {
        case DeviceIoMemoryBased: {
            uintptr_t BaseAddress = ioEntry->Io.Access.Memory.PhysicalBase;
            size_t    PageSize    = GetMemorySpacePageSize();
            size_t    Length      = ioEntry->Io.Access.Memory.Length + (BaseAddress % PageSize);
            assert(memorySpace->Context != NULL);
            MemorySpaceUnmap(memorySpace, ioEntry->MappedAddress, Length);
        } break;

        case DeviceIoPortBased: {
            for (size_t i = 0; i < ioEntry->Io.Access.Port.Length; i++) {
                SetDirectIoAccess(coreId, memorySpace, ((uint16_t)(ioEntry->Io.Access.Port.Base + i)), 0);
            }
        } break;

        default:
            ERROR(" > unimplemented device-io type %" PRIuIN "", ioEntry->Io.Type);
            break;
    }
    devIO->Access.Memory.VirtualBase      = 0;
    ioEntry->MappedAddress                 = 0;
    ioEntry->Owner                         = UUID_INVALID;
    return OS_EOK;
}

oserr_t
CreateKernelSystemDeviceIo(
    _In_  DeviceIo_t*  SourceIoSpace,
    _Out_ DeviceIo_t** SystemIoSpace)
{
    DeviceIOEntry_t* ioEntry;

    ioEntry = (DeviceIOEntry_t*)list_find_value(&g_ioSpaces, VOID_KEY(SourceIoSpace->Id));
    if (!ioEntry) {
        return OS_EUNKNOWN;
    }

    switch (ioEntry->Io.Type) {
        case DeviceIoMemoryBased: {
            paddr_t physicalBase = ioEntry->Io.Access.Memory.PhysicalBase;
            size_t  pageSize     = GetMemorySpacePageSize();
            size_t  length       = ioEntry->Io.Access.Memory.Length + (physicalBase % pageSize);
            oserr_t oserr        = MemorySpaceMap(
                    GetCurrentMemorySpace(),
                    &(struct MemorySpaceMapOptions) {
                        .PhysicalStart = physicalBase,
                        .Length = length,
                        .Flags = MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_PERSISTENT,
                        .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
                    },
                    &ioEntry->Io.Access.Memory.VirtualBase
            );
            if (oserr != OS_EOK) {
                ERROR(" > failed to create mapping");
                return OS_EUNKNOWN;
            }
        } break;

        // Both port and port/pin are not needed to remap here
        default:
            break;
    }
    *SystemIoSpace = &ioEntry->Io;
    return OS_EOK;
}

oserr_t
ReleaseKernelSystemDeviceIo(
    _In_ DeviceIo_t* SystemIoSpace)
{
    DeviceIOEntry_t* ioEntry;

    ioEntry = (DeviceIOEntry_t*)list_find_value(&g_ioSpaces, VOID_KEY(SystemIoSpace->Id));
    if (!ioEntry) {
        return OS_EUNKNOWN;
    }

    switch (ioEntry->Io.Type) {
        case DeviceIoMemoryBased: {
            uintptr_t BaseAddress   = ioEntry->Io.Access.Memory.PhysicalBase;
            size_t PageSize         = GetMemorySpacePageSize();
            size_t Length           = ioEntry->Io.Access.Memory.Length + (BaseAddress % PageSize);
            MemorySpaceUnmap(GetCurrentMemorySpace(), ioEntry->Io.Access.Memory.VirtualBase, Length);
            ioEntry->Io.Access.Memory.VirtualBase = 0;
        } break;

        // Both port and port/pin are not needed to remap here
        default:
            break;
    }
    return OS_EOK;
}

// @interrupt context
uintptr_t
ValidateDeviceIoMemoryAddress(
    _In_ uintptr_t Address)
{
    TRACE("ValidateDeviceIoMemoryAddress(Process %" PRIuIN ", Address 0x%" PRIxIN ")", Handle, Address);

    // Iterate and check each io-space if the process has this mapped in
    foreach(i, &g_ioSpaces) {
        DeviceIOEntry_t* IoSpace     = (DeviceIOEntry_t*)i->value;
        uintptr_t         VirtualBase = IoSpace->MappedAddress;

        // Two things has to be true before the io-space
        // is valid, it has to belong to the right process
        // and be in range 
        if (IoSpace->Owner == GetCurrentMemorySpaceHandle() && IoSpace->Io.Type == DeviceIoMemoryBased &&
            (Address >= VirtualBase && Address < (VirtualBase + IoSpace->Io.Access.Memory.Length))) {
            return IoSpace->Io.Access.Memory.PhysicalBase + (Address - VirtualBase);
        }
    }
    return 0;
}
