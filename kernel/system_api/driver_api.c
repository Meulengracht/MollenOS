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
#include <handle.h>
#include <interrupts.h>
#include <machine.h>

oscode_t
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
        return OsOK;
    }
}

oscode_t
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
    return OsOK;
}

oscode_t
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
    return OsOK;
}

oscode_t
ScAcpiQueryInterrupt(
    _In_  int           bus,
    _In_  int           device,
    _In_  int           pin,
    _Out_ int*          interruptOut,
    _Out_ unsigned int* acpiConformOut)
{
    return AcpiDeviceGetInterrupt(bus, device, pin, interruptOut, acpiConformOut);
}

oscode_t
ScIoSpaceRegister(
    _In_ DeviceIo_t* ioSpace)
{
    if (ioSpace == NULL) {
        return OsError;
    }
    return RegisterSystemDeviceIo(ioSpace);
}

oscode_t
ScIoSpaceAcquire(
    _In_ DeviceIo_t* IoSpace)
{
    if (IoSpace == NULL) {
        return OsError;
    }
    return AcquireSystemDeviceIo(IoSpace);
}

oscode_t
ScIoSpaceRelease(
    _In_ DeviceIo_t* ioSpace)
{
    if (ioSpace == NULL) {
        return OsError;
    }
    return ReleaseSystemDeviceIo(ioSpace);
}

oscode_t
ScIoSpaceDestroy(
    _In_ DeviceIo_t* ioSpace)
{
    if (ioSpace == NULL) {
        return OsInvalidParameters;
    }
    DestroyHandle(ioSpace->Id);
    return OsOK;
}

uuid_t
ScRegisterInterrupt(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ unsigned int       flags)
{
    if (deviceInterrupt == NULL ||
        (flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT))) {
        return UUID_INVALID;
    }
    return InterruptRegister(deviceInterrupt, flags);
}

oscode_t
ScUnregisterInterrupt(
        _In_ uuid_t sourceId)
{
    return InterruptUnregister(sourceId);
}

oscode_t
ScGetProcessBaseAddress(
    _Out_ uintptr_t* baseAddress)
{
    if (baseAddress != NULL) {
        *baseAddress = GetMachine()->MemoryMap.UserCode.Start;
        return OsOK;
    }
    return OsInvalidParameters;
}
