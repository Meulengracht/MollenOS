/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - ACPI Support Definitions & Structures
 * - This header describes the base acpi-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <stdlib.h>
#include <string.h>

oserr_t
AcpiQueryStatus(
    _In_ AcpiDescriptor_t* AcpiDescriptor)
{
    OSFirmwareInfo_t info;
    oserr_t          oserr;

    if (AcpiDescriptor == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = FirmwareQuery(&info);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (!(info.Available & OSFIRMWARE_ACPI)) {
        return OS_ENOTSUPPORTED;
    }

    AcpiDescriptor->Version      = info.Acpi.Revision;
    AcpiDescriptor->Century      = info.Acpi.Century;
    AcpiDescriptor->BootFlags    = info.Acpi.IaBootFlags;
    AcpiDescriptor->ArmBootFlags = info.Acpi.ArmBootFlags;
    return OS_EOK;
}

oserr_t
AcpiQueryTable(
    _In_  const char*         signature,
    _Out_ ACPI_TABLE_HEADER** tableOut)
{
    OSFirmwareTableKey_t key = { .Source = OSFIRMWARE_ACPI };
    OSFirmwareTable_t    table;
    void*                buffer;
    size_t               length;
    oserr_t              oserr;

    if (signature == NULL || tableOut == NULL || strlen(signature) != sizeof(key.Signature)) {
        return OS_EINVALPARAMS;
    }
    memcpy(&key.Signature[0], signature, sizeof(key.Signature));

    oserr = FirmwareTableLocate(&key, &table);
    if (oserr != OS_EOK) {
        return oserr;
    }

    buffer = malloc(table.Length);
    if (!buffer) {
        return OS_EOOM;
    }

    oserr = FirmwareTableRead(&key, buffer, table.Length, &length);
    if (oserr != OS_EOK) {
        free(buffer);
        return oserr;
    }

    *tableOut = buffer;
    return OS_EOK;
}

oserr_t AcpiQueryInterrupt(
    _In_  unsigned int Bus,
    _In_  unsigned int Device,
    _In_  int       Pin,
    _Out_ int*      Interrupt,
    _Out_ unsigned int*  AcpiConform)
{
    if (Interrupt == NULL || AcpiConform == NULL) {
        return OS_EUNKNOWN;
    }
    return Syscall_AcpiQueryInterrupt(Bus, Device, Pin, Interrupt, AcpiConform);
}
