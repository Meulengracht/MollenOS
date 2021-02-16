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
#include <modules/manager.h>
#include <interrupts.h>
#include <machine.h>

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
    _In_ const char*        signature,
    _In_ ACPI_TABLE_HEADER* header)
{
    if (!signature || !header) {
        return OsInvalidParameters;
    }

    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsNotSupported;
    }

    if (ACPI_FAILURE(AcpiGetTableHeader((ACPI_STRING)signature, 0, header))) {
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
ScAcpiQueryTable(
    _In_ const char*        signature,
    _In_ ACPI_TABLE_HEADER* table)
{
    ACPI_TABLE_HEADER* header = NULL;

    if (!signature || !table) {
        return OsInvalidParameters;
    }

    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsNotSupported;
    }

    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)signature, 0, &header))) {
        return OsError;
    }

    memcpy(table, header, header->Length);
    return OsSuccess;
}

OsStatus_t
ScAcpiQueryInterrupt(
    _In_  int           bus,
    _In_  int           device,
    _In_  int           pin,
    _Out_ int*          interruptOut,
    _Out_ unsigned int* acpiConformOut)
{
    return AcpiDeviceGetInterrupt(bus, device, pin, interruptOut, acpiConformOut);
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
        return OsInvalidParameters;
    }
    DestroyHandle(IoSpace->Id);
    return OsSuccess;
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
    _In_ Device_t*   device,
    _In_ const void* driverBuffer,
    _In_ size_t      driverBufferLength)
{
    SystemModule_t* currentModule = GetCurrentModule();
    SystemModule_t* module;
    OsStatus_t osStatus;

    TRACE("ScLoadDriver(Vid 0x%" PRIxIN ", Pid 0x%" PRIxIN ", Class 0x%" PRIxIN ", Subclass 0x%" PRIxIN ")",
          device->VendorId, device->DeviceId, device->Class, device->Subclass);
    if (!currentModule || !device) {
        if (!currentModule) {
            return OsInvalidPermissions;
        }
        return OsInvalidParameters;
    }

    // First of all, if a server has already been spawned
    // for the specific driver, then call it's RegisterInstance
    module = GetModule(device->VendorId, device->DeviceId, device->Class, device->Subclass);
    if (!module) {
        // Look for matching driver first, then generic
        module = GetSpecificDeviceModule(device->VendorId, device->DeviceId);
        module = (module == NULL) ? GetGenericDeviceModule(device->Class, device->Subclass) : module;

        // We did not have any, did the driver provide one for us?
        if (!module) {
            if (driverBuffer && driverBufferLength ) {
                osStatus = RegisterModule("custom_module", driverBuffer, driverBufferLength, ModuleResource,
                                          device->VendorId, device->DeviceId, device->Class, device->Subclass);
                if (osStatus == OsSuccess) {
                    module = GetModule(device->VendorId, device->DeviceId, device->Class, device->Subclass);
                }
            }

            if (!module) {
                return OsDoesNotExist;
            }
        }

        osStatus = SpawnModule(module);
        if (osStatus != OsSuccess) {
            return osStatus;
        }
    }
    return OsSuccess;
}

UUId_t
ScRegisterInterrupt(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ unsigned int            Flags)
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
