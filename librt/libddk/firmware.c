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
#include <ddk/firmware.h>
#include <os/memory.h>

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
