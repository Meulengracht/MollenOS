/**
 * Copyright 2026, Philip Meulengracht
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
 * Firmware table interface (ACPI and DeviceTree)
 */

#include <internal/_syscalls.h>
#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <os/memory.h>
#include <stdlib.h>
#include <string.h>

oserr_t
FirmwareQuery(
    _Out_ OSFirmwareInfo_t* infoOut)
{
    if (infoOut == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_FirmwareQuery(infoOut);
}

oserr_t
FirmwareTableLocate(
    _In_  const OSFirmwareTableKey_t* key,
    _Out_ OSFirmwareTable_t*          tableOut)
{
    if (key == NULL || tableOut == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_FirmwareTableLocate(key, tableOut);
}

oserr_t
FirmwareTableRead(
    _In_  const OSFirmwareTableKey_t* key,
    _In_  void*                       buffer,
    _In_  size_t                      size,
    _Out_ size_t*                     lengthOut)
{
    if (key == NULL || lengthOut == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_FirmwareTableRead(key, buffer, size, lengthOut);
}

oserr_t
FirmwareTableMap(
    _In_  const OSFirmwareTableKey_t* key,
    _Out_ const void**                mappingOut,
    _Out_ size_t*                     lengthOut)
{
    if (key == NULL || mappingOut == NULL || lengthOut == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_FirmwareTableMap(key, mappingOut, lengthOut);
}

oserr_t
FirmwareTableUnmap(
    _In_ const void* mapping,
    _In_ size_t      length)
{
    if (mapping == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }
    return MemoryFree((void*)mapping, length);
}

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

oserr_t
AcpiQueryPci(
    _In_  const struct AcpiPciQuery* query,
    _Out_ union AcpiPciResult*      result)
{
    if (query == NULL || result == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_AcpiQueryPci(query, result);
}
