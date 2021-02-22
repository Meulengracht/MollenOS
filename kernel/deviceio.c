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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * IO Space Interface
 * - Contains the shared kernel io space interface
 *   that all sub-layers / architectures must conform to
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
#include <modules/manager.h>
#include <threading.h>
#include <string.h>

#define VOID_KEY(Key) (void*)(uintptr_t)Key

typedef struct SystemDeviceIo {
    element_t  Header;
    DeviceIo_t Io;
    UUId_t     Owner;
    uintptr_t  MappedAddress;
} SystemDeviceIo_t;

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
    SystemDeviceIo_t*     systemIo      = element->value;
    struct VerifyContext* verifyContext = context;

    if (verifyContext->IoSpace->Type != systemIo->Io.Type) {
        return LIST_ENUMERATE_CONTINUE;
    }

    if (systemIo->Io.Type == DeviceIoPortBased) {
        size_t systemIoStart = systemIo->Io.Access.Port.Base;
        size_t systemIoLimit = systemIo->Io.Access.Port.Base + systemIo->Io.Access.Port.Length;
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
    else if (systemIo->Io.Type == DeviceIoMemoryBased) {
        size_t systemIoStart = systemIo->Io.Access.Memory.PhysicalBase;
        size_t systemIoLimit = systemIo->Io.Access.Memory.PhysicalBase + systemIo->Io.Access.Memory.Length;
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
    SystemDeviceIo_t* SystemIo;
    
    TRACE("DestroySystemDeviceIo(Id %" PRIuIN ")", IoSpace);

    SystemIo = (SystemDeviceIo_t*)list_find_value(&g_ioSpaces, VOID_KEY(IoSpace->Id));
    if (SystemIo == NULL || SystemIo->Owner != UUID_INVALID) {
        return;
    }
    
    list_remove(&g_ioSpaces, list_find(&g_ioSpaces, VOID_KEY(IoSpace->Id)));
    kfree(SystemIo);
}

OsStatus_t
RegisterSystemDeviceIo(
    _In_ DeviceIo_t* ioSpace)
{
    SystemDeviceIo_t*    systemIo;
    struct VerifyContext context = { .IoSpace = ioSpace, .Valid = 1 };
    TRACE("RegisterSystemDeviceIo(ioSpace=0x%" PRIxIN ")", ioSpace);

    if (!ioSpace) {
        return OsInvalidParameters;
    }

    // Before doing anything, we should do a over-lap
    // check before trying to register this
    list_enumerate(&g_ioSpaces, __DetectIoOverlaps, &context);
    if (!context.Valid) {
        return OsBusy;
    }

    // Allocate a new system only copy of the io-space
    // as we don't want anyone to edit our copy
    systemIo = (SystemDeviceIo_t*)kmalloc(sizeof(SystemDeviceIo_t));
    if (!systemIo) {
        return OsOutOfMemory;
    }
    
    memset(systemIo, 0, sizeof(SystemDeviceIo_t));
    systemIo->Owner = UUID_INVALID;
    ioSpace->Id     = CreateHandle(HandleTypeGeneric, DestroySystemDeviceIo, systemIo);
    ELEMENT_INIT(&systemIo->Header, VOID_KEY(ioSpace->Id), systemIo);
    memcpy(&systemIo->Io, ioSpace, sizeof(DeviceIo_t));
    
    return list_append(&g_ioSpaces, &systemIo->Header);
}

OsStatus_t
AcquireSystemDeviceIo(
    _In_ DeviceIo_t* IoSpace)
{
    SystemDeviceIo_t *    SystemIo;
    MemorySpace_t    * Space       = GetCurrentMemorySpace();
    SystemModule_t   *      Module = GetCurrentModule();
    UUId_t               CoreId = ArchGetProcessorCoreId();
    assert(IoSpace != NULL);

    TRACE("AcquireSystemDeviceIo(Id %" PRIuIN ")", IoSpace->Id);

    // Lookup the system copy to validate this requested operation
    SystemIo = (SystemDeviceIo_t*)list_find_value(&g_ioSpaces, VOID_KEY(IoSpace->Id));

    // Sanitize the system copy
    if (Module == NULL || SystemIo == NULL || SystemIo->Owner != UUID_INVALID) {
        if (Module == NULL) {
            ERROR(" > non-server process tried to acquire io-space");
        }
        ERROR(" > failed to find the requested io-space, id %" PRIuIN "", IoSpace->Id);
        return OsError;
    }
    SystemIo->Owner = Module->Handle;

    switch (SystemIo->Io.Type) {
        case DeviceIoMemoryBased: {
            uintptr_t MappedAddress;
            uintptr_t BaseAddress = SystemIo->Io.Access.Memory.PhysicalBase;
            size_t    PageSize    = GetMemorySpacePageSize();
            size_t    Length      = SystemIo->Io.Access.Memory.Length + (BaseAddress % PageSize);
            OsStatus_t Status     = MemorySpaceMapContiguous(GetCurrentMemorySpace(),
                &MappedAddress, BaseAddress, Length, 
                MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT, 
                MAPPING_VIRTUAL_PROCESS);
            if (Status != OsSuccess) {
                ERROR(" > Failed to allocate memory for device io memory");
                SystemIo->Owner = UUID_INVALID;
                return Status;
            }

            // Adjust for offset and store in io copies
            MappedAddress                       += BaseAddress % PageSize;
            IoSpace->Access.Memory.VirtualBase   = MappedAddress;
            SystemIo->MappedAddress              = MappedAddress;
            return OsSuccess;
        } break;

        case DeviceIoPortBased: {
            for (size_t i = 0; i < SystemIo->Io.Access.Port.Length; i++) {
                SetDirectIoAccess(CoreId, Space, ((uint16_t)(SystemIo->Io.Access.Port.Base + i)), 1);
            }
            return OsSuccess;
        } break;

        default:
            ERROR(" > unimplemented device-io type %" PRIuIN "", SystemIo->Io.Type);
            break;
    }
    SystemIo->Owner = UUID_INVALID;
    return OsError;
}

OsStatus_t
ReleaseSystemDeviceIo(
    _In_ DeviceIo_t*    IoSpace)
{
    SystemDeviceIo_t *    SystemIo;
    MemorySpace_t    * Space       = GetCurrentMemorySpace();
    SystemModule_t   *      Module = GetCurrentModule();
    UUId_t               CoreId = ArchGetProcessorCoreId();
    
    assert(IoSpace != NULL);
    TRACE("ReleaseSystemDeviceIo(Id %" PRIuIN ")", IoSpace->Id);

    SystemIo = (SystemDeviceIo_t*)list_find_value(&g_ioSpaces, VOID_KEY(IoSpace->Id));

    // Sanitize the system copy and do some security checks
    if (Module == NULL || SystemIo == NULL || 
        SystemIo->Owner != Module->Handle) {
        if (Module == NULL) {
            ERROR(" > non-server process tried to acquire io-space");
        }
        ERROR(" > failed to find the requested io-space, id %" PRIuIN "", IoSpace->Id);
        return OsError;
    }

    switch (SystemIo->Io.Type) {
        case DeviceIoMemoryBased: {
            uintptr_t BaseAddress = SystemIo->Io.Access.Memory.PhysicalBase;
            size_t    PageSize    = GetMemorySpacePageSize();
            size_t    Length      = SystemIo->Io.Access.Memory.Length + (BaseAddress % PageSize);
            assert(Space->Context != NULL);
            MemorySpaceUnmap(Space, SystemIo->MappedAddress, Length);
        } break;

        case DeviceIoPortBased: {
            for (size_t i = 0; i < SystemIo->Io.Access.Port.Length; i++) {
                SetDirectIoAccess(CoreId, Space, ((uint16_t)(SystemIo->Io.Access.Port.Base + i)), 0);
            }
        } break;

        default:
            ERROR(" > unimplemented device-io type %" PRIuIN "", SystemIo->Io.Type);
            break;
    }
    IoSpace->Access.Memory.VirtualBase      = 0;
    SystemIo->MappedAddress                 = 0;
    SystemIo->Owner                         = UUID_INVALID;
    return OsSuccess;
}

OsStatus_t
CreateKernelSystemDeviceIo(
    _In_  DeviceIo_t*  SourceIoSpace,
    _Out_ DeviceIo_t** SystemIoSpace)
{
    SystemDeviceIo_t* SystemIo;
    
    SystemIo = (SystemDeviceIo_t*)list_find_value(&g_ioSpaces, VOID_KEY(SourceIoSpace->Id));
    if (!SystemIo) {
        return OsError;
    }

    switch (SystemIo->Io.Type) {
        case DeviceIoMemoryBased: {
            uintptr_t BaseAddress = SystemIo->Io.Access.Memory.PhysicalBase;
            size_t PageSize       = GetMemorySpacePageSize();
            size_t Length         = SystemIo->Io.Access.Memory.Length + (BaseAddress % PageSize);
            OsStatus_t Status     = MemorySpaceMapContiguous(GetCurrentMemorySpace(),
                &SystemIo->Io.Access.Memory.VirtualBase, BaseAddress, Length, 
                MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_PERSISTENT, 
                MAPPING_VIRTUAL_GLOBAL);
            if (Status != OsSuccess) {
                ERROR(" > failed to create mapping");
                return OsError;
            }
        } break;

        // Both port and port/pin are not needed to remap here
        default:
            break;
    }
    *SystemIoSpace = &SystemIo->Io;
    return OsSuccess;
}

OsStatus_t
ReleaseKernelSystemDeviceIo(
    _In_ DeviceIo_t* SystemIoSpace)
{
    SystemDeviceIo_t* SystemIo;
    
    SystemIo = (SystemDeviceIo_t*)list_find_value(&g_ioSpaces, VOID_KEY(SystemIoSpace->Id));
    if (!SystemIo) {
        return OsError;
    }

    switch (SystemIo->Io.Type) {
        case DeviceIoMemoryBased: {
            uintptr_t BaseAddress   = SystemIo->Io.Access.Memory.PhysicalBase;
            size_t PageSize         = GetMemorySpacePageSize();
            size_t Length           = SystemIo->Io.Access.Memory.Length + (BaseAddress % PageSize);
            MemorySpaceUnmap(GetCurrentMemorySpace(), SystemIo->Io.Access.Memory.VirtualBase, Length);
            SystemIo->Io.Access.Memory.VirtualBase = 0;
        } break;

        // Both port and port/pin are not needed to remap here
        default:
            break;
    }
    return OsSuccess;
}

// @interrupt context
uintptr_t
ValidateDeviceIoMemoryAddress(
    _In_ uintptr_t Address)
{
    SystemModule_t* Module = GetCurrentModule();
    TRACE("ValidateDeviceIoMemoryAddress(Process %" PRIuIN ", Address 0x%" PRIxIN ")", Handle, Address);

    if (Module == NULL) {
        return 0;
    }
    
    // Iterate and check each io-space if the process has this mapped in
    foreach(i, &g_ioSpaces) {
        SystemDeviceIo_t* IoSpace     = (SystemDeviceIo_t*)i->value;
        uintptr_t         VirtualBase = IoSpace->MappedAddress;

        // Two things has to be true before the io-space
        // is valid, it has to belong to the right process
        // and be in range 
        if (IoSpace->Owner == Module->Handle && IoSpace->Io.Type == DeviceIoMemoryBased &&
            (Address >= VirtualBase && Address < (VirtualBase + IoSpace->Io.Access.Memory.Length))) {
            return IoSpace->Io.Access.Memory.PhysicalBase + (Address - VirtualBase);
        }
    }
    return 0;
}
