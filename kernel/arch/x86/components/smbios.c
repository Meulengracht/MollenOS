/* MollenOS
 *
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

/* Includes
 * - System */
#include <smbios.h>
#include <string.h>

// Globals
static SmBiosTable_t SmBiosHeader = { 0 };

/* SmBiosLocate
 * Locates the SMBIOS entry point structure in lower memory. */
OsStatus_t
SmBiosLocate(void)
{
    // Search for magic string on 16 byte boundaries
    for (uintptr_t Address = SMBIOS_DEFAULT_MEMORY_LOCATION_START; 
        Address < SMBIOS_DEFAULT_MEMORY_LOCATION_END; Address += 0x10)
    {
        if (*((uintptr_t*)Address) == SMBIOS_SIGNATURE) {
            memcpy((void*)&SmBiosHeader, (const void*)Address, sizeof(SmBiosTable_t));
            return OsSuccess;
        }
    }
    return OsError;
}

/* SmBiosInitialize
 * Initializes and finds if the smbios table is present on the system. The 
 * function will return OsSuccess if the table is present and everything is alright. */
OsStatus_t
SmBiosInitialize(void *UfiConfigurationTable)
{
    // If the configuration table is NULL this is a non-ufi system
    // and we should locate it in memory
    if (UfiConfigurationTable == NULL) {
        return SmBiosLocate();
    }
    else {
        // @todo
    }
    return OsError;
}
