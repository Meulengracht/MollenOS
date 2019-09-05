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
 * System Calls
 */

#define __MODULE "SCIF"
//#define __TRACE

#include <acpiinterface.h>
#include <arch/utils.h>
#include <ddk/acpi.h>
#include <ddk/device.h>
#include <debug.h>
#include <deviceio.h>
#include <handle.h>
#include <heap.h>
#include <os/input.h>
#include <os/ipc.h>
#include <modules/manager.h>
#include <interrupts.h>
#include <machine.h>
#include <timers.h>

extern OsStatus_t ScIpcInvoke(UUId_t, IpcMessage_t*, unsigned int, size_t, void**);

OsStatus_t
ScAcpiQueryStatus(
   _In_ AcpiDescriptor_t*   AcpiDescriptor)
{
    if (AcpiDescriptor == NULL) {
        return OsError;
    }

    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    else {
        AcpiDescriptor->Century         = AcpiGbl_FADT.Century;
        AcpiDescriptor->BootFlags       = AcpiGbl_FADT.BootFlags;
        AcpiDescriptor->ArmBootFlags    = AcpiGbl_FADT.ArmBootFlags;
        AcpiDescriptor->Version         = ACPI_VERSION_6_0;
        return OsSuccess;
    }
}

OsStatus_t
ScAcpiQueryTableHeader(
    _In_ const char*        Signature,
    _In_ ACPI_TABLE_HEADER* Header)
{
    ACPI_TABLE_HEADER *PointerToHeader = NULL;

    // Sanitize some statuses
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)Signature, 0, &PointerToHeader))) {
        return OsError;
    }

    memcpy(Header, PointerToHeader, sizeof(ACPI_TABLE_HEADER));
    return OsSuccess;
}

OsStatus_t
ScAcpiQueryTable(
    _In_ const char*        Signature,
    _In_ ACPI_TABLE_HEADER* Table)
{
    ACPI_TABLE_HEADER *Header = NULL;

    // Sanitize some statuses
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)Signature, 0, &Header))) {
        return OsError;
    }

    memcpy(Header, Table, Header->Length);
    return OsSuccess;
}

OsStatus_t
ScAcpiQueryInterrupt(
    _In_  DevInfo_t         Bus,
    _In_  DevInfo_t         Device,
    _In_  int               Pin, 
    _Out_ int*              Interrupt,
    _Out_ Flags_t*          AcpiConform)
{
    *Interrupt = AcpiDeriveInterrupt(Bus, Device, Pin, AcpiConform);
    return (*Interrupt == INTERRUPT_NONE) ? OsError : OsSuccess;
}

OsStatus_t
ScIoSpaceRegister(
    _In_ DeviceIo_t* IoSpace)
{
    SystemModule_t* Module = GetCurrentModule();
    if (IoSpace == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    return RegisterSystemDeviceIo(IoSpace);
}

OsStatus_t
ScIoSpaceAcquire(
    _In_ DeviceIo_t* IoSpace)
{
    SystemModule_t* Module = GetCurrentModule();
    if (IoSpace == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    return AcquireSystemDeviceIo(IoSpace);
}

OsStatus_t
ScIoSpaceRelease(
    _In_ DeviceIo_t* IoSpace)
{
    SystemModule_t* Module = GetCurrentModule();
    if (IoSpace == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    return ReleaseSystemDeviceIo(IoSpace);
}

OsStatus_t
ScIoSpaceDestroy(
    _In_ DeviceIo_t* IoSpace)
{
    SystemModule_t* Module = GetCurrentModule();
    if (IoSpace == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    return DestroySystemDeviceIo(IoSpace);
}

OsStatus_t
ScRegisterAliasId(
    _In_ UUId_t Alias)
{
    WARNING("New Service: 0x%" PRIxIN "", Alias);
    return SetModuleAlias(Alias);
}

OsStatus_t
ScLoadDriver(
    _In_ MCoreDevice_t* Device,
    _In_ size_t         LengthOfDeviceStructure,
    _In_ const void*    DriverBuffer,
    _In_ size_t         DriverBufferLength)
{
    SystemModule_t* CurrentModule = GetCurrentModule();
    SystemModule_t* Module;
    IpcMessage_t    Message;
    OsStatus_t      Status;

    TRACE("ScLoadDriver(Vid 0x%" PRIxIN ", Pid 0x%" PRIxIN ", Class 0x%" PRIxIN ", Subclass 0x%" PRIxIN ")",
        Device->VendorId, Device->DeviceId, Device->Class, Device->Subclass);
    if (CurrentModule == NULL || Device == NULL || LengthOfDeviceStructure < sizeof(MCoreDevice_t)) {
        if (CurrentModule == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }

    // First of all, if a server has already been spawned
    // for the specific driver, then call it's RegisterInstance
    Module = GetModule(Device->VendorId, Device->DeviceId, Device->Class, Device->Subclass);
    if (Module == NULL) {
        // Look for matching driver first, then generic
        Module = GetSpecificDeviceModule(Device->VendorId, Device->DeviceId);
        Module = (Module == NULL) ? GetGenericDeviceModule(Device->Class, Device->Subclass) : Module;

        // We did not have any, did the driver provide one for us?
        if (Module == NULL) {
            if (DriverBuffer != NULL && DriverBufferLength != 0) {
                Status = RegisterModule("custom_module", DriverBuffer, DriverBufferLength, ModuleResource, 
                    Device->VendorId, Device->DeviceId, Device->Class, Device->Subclass);
                if (Status == OsSuccess) {
                    Module = GetModule(Device->VendorId, Device->DeviceId, Device->Class, Device->Subclass);
                }
            }

            if (Module == NULL) {
                return OsError;
            }
        }

        Status = SpawnModule(Module);
        if (Status != OsSuccess) {
            return Status;
        }
    }

    IpcInitialize(&Message);
    IpcSetTypedArgument(&Message, 0, __DRIVER_REGISTERINSTANCE);
    IpcSetUntypedArgument(&Message, 0, Device, LengthOfDeviceStructure);
    return ScIpcInvoke(Module->PrimaryThreadId, &Message, IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}

UUId_t
ScRegisterInterrupt(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ Flags_t            Flags)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Interrupt == NULL || Module == NULL || 
        (Flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT))) {
        return UUID_INVALID;
    }
    return InterruptRegister(Interrupt, Flags);
}

OsStatus_t
ScUnregisterInterrupt(
    _In_ UUId_t Source)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module == NULL) {
        return OsInvalidPermissions;
    }
    return InterruptUnregister(Source);
}

OsStatus_t
ScGetProcessBaseAddress(
    _Out_ uintptr_t* BaseAddress)
{
    if (BaseAddress != NULL) {
        *BaseAddress = GetMachine()->MemoryMap.UserCode.Start;
        return OsSuccess;
    }
    return OsInvalidParameters;
}
