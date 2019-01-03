/* MollenOS
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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <modules/manager.h>
#include <system/utils.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <deviceio.h>
#include <os/input.h>
#include <os/acpi.h>
#include <machine.h>
#include <handle.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>
#include <pipe.h>

extern OsStatus_t ScRpcExecute(MRemoteCall_t* RemoteCall, int Async);

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
    WARNING("New Service: 0x%x", Alias);
    return SetModuleAlias(Alias);
}

OsStatus_t
ScLoadDriver(
    _In_ MCoreDevice_t* Device,
    _In_ size_t         LengthOfDeviceStructure,
    _In_ const void*    DriverBuffer,
    _In_ size_t         DriverBufferLength)
{
    MRemoteCall_t   RemoteCall    = { UUID_INVALID, { 0 }, 0 };
    SystemModule_t* CurrentModule = GetCurrentModule();
    SystemModule_t* Module;
    OsStatus_t      Status;

    TRACE("ScLoadDriver(Vid 0x%x, Pid 0x%x, Class 0x%x, Subclass 0x%x)",
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

        Status = SpawnModule(Module, DriverBuffer, DriverBufferLength);
        if (Status != OsSuccess) {
            return Status;
        }
    }

    // Initialize the base of a new message, always protocol version 1
    RPCInitialize(&RemoteCall, Module->Handle, 1, __DRIVER_REGISTERINSTANCE);
    RPCSetArgument(&RemoteCall, 0, Device, LengthOfDeviceStructure);
    return ScRpcExecute(&RemoteCall, 1);
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
ScRegisterEventTarget(
    _In_ UUId_t KeyInput,
    _In_ UUId_t WmInput)
{
    GetMachine()->StdInput = (SystemPipe_t*)LookupHandle(KeyInput);
    GetMachine()->WmInput  = (SystemPipe_t*)LookupHandle(WmInput);
    return OsSuccess;
}

OsStatus_t
ScKeyEvent(
    _In_ SystemKey_t* Key)
{
    if (GetMachine()->StdInput != NULL) {
        return WriteSystemPipe(GetMachine()->StdInput, (const uint8_t*)Key, sizeof(SystemKey_t));
    }
    return OsSuccess;
}

OsStatus_t
ScInputEvent(
    _In_ SystemInput_t* Input)
{
    if (GetMachine()->WmInput != NULL) {
        return WriteSystemPipe(GetMachine()->WmInput, (const uint8_t*)Input, sizeof(SystemInput_t));
    }
    return OsSuccess;
}

UUId_t
ScTimersStart(
    _In_ size_t      Interval,
    _In_ int         Periodic,
    _In_ const void* Data)
{
    return TimersStart(Interval, Periodic, Data);
}

OsStatus_t
ScTimersStop(
    _In_ UUId_t TimerId)
{
    return TimersStop(TimerId);
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