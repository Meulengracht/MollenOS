/**
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS X86 SMBIOS Driver
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_2.8.0.pdf
 */

#include <machine.h>
#include <smbios.h>
#include <string.h>
#include <utils/uefi.h>

static __EFI_GUID    g_smbiosGuid  = SMBIOS_TABLE_GUID;
static __EFI_GUID    g_smbios3Guid = SMBIOS3_TABLE_GUID;
static SmBiosTable_t g_smbios      = { 0 };

OsStatus_t
SmBiosLocate(void)
{
    // Search for magic string on 16 byte boundaries
    for (uintptr_t Address = SMBIOS_DEFAULT_MEMORY_LOCATION_START; 
        Address < SMBIOS_DEFAULT_MEMORY_LOCATION_END; Address += 0x10)
    {
        if (*((uintptr_t*)Address) == SMBIOS_SIGNATURE) {
            memcpy((void*)&g_smbios, (const void*)Address, sizeof(SmBiosTable_t));
            return OsSuccess;
        }
    }
    return OsError;
}

static OsStatus_t
__FindSmBiosInConfigurationTable(
        _In_ void*        configurationTable,
        _In_ unsigned int count)
{
    void* table;

    if (!configurationTable || !count) {
        return OsDoesNotExist;
    }

    table = __LocateGuidInConfigurationTable(configurationTable, count, g_smbios3Guid);
    if (table) {
        memcpy((void*)&g_smbios, (const void*)table, sizeof(SmBiosTable_t));
        return OsSuccess;
    }

    table = __LocateGuidInConfigurationTable(configurationTable, count, g_smbiosGuid);
    if (table) {
        memcpy((void*)&g_smbios, (const void*)table, sizeof(SmBiosTable_t));
        return OsSuccess;
    }
    return OsDoesNotExist;
}

OsStatus_t
SmBiosInitialize(void)
{
    // If the configuration table is NULL this is a non-ufi system
    // and we should locate it in memory
    OsStatus_t osStatus = __FindSmBiosInConfigurationTable(GetMachine()->BootInformation.ConfigurationTable,
                                                           GetMachine()->BootInformation.ConfigurationTableCount);
    if (osStatus != OsSuccess) {
        return SmBiosLocate();
    }
    return osStatus;
}
