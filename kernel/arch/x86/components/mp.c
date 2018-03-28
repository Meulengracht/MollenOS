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
 * MollenOS X86 MP-Tables Parser
 * https://wiki.osdev.org/Symmetric_Multiprocessing#Finding_information_using_MP_Table
 */

/* Includes
 * - System */
#include <mp.h>
#include <string.h>

#define EBDA_START      0x40E
#define EBDA_SEARCH_SZ  0x400

#define BASE_START      0xa0000
#define BASE_SEARCH_SZ  0x400

#define ROM_START       0xf0000
#define ROM_END         0x100000
#define SEARCH_AREA(Start, End)     for(i = (uintptr_t*)Start; (uintptr_t)i < (uintptr_t)End; i++) { \
                                        if(*i == MP_HEADER_MAGIC) { \
                                            memcpy((void*)&MpHeader, (void*)i, sizeof(MpHeader_t)); \
                                            i = (uintptr_t*)(uintptr_t)MpHeader.ConfigurationTableAddress; \
                                            memcpy((void*)&MpConfigurationTable, (void*)i, sizeof(MpConfigurationTable_t)); \
                                            return OsSuccess; \
                                        } \
                                    } \

// Globals
// State-keeping
static MpHeader_t MpHeader                          = { 0 };
static MpConfigurationTable_t MpConfigurationTable  = { 0 };

/* MpInitialize
 * Searches known memory places where the mp-tables can exist, returns 
 * OsSuccess if they exist - otherwise OsError */
OsStatus_t
MpInitialize(void)
{
   uintptr_t *i = NULL;
   SEARCH_AREA(0, 0x400) // Search the lower 1k
   SEARCH_AREA(EBDA_START, EBDA_START + EBDA_SEARCH_SZ)
   SEARCH_AREA(BASE_START, BASE_START + BASE_SEARCH_SZ)
   SEARCH_AREA(ROM_START, ROM_END)
   return OsError;
}

/* MpGetLocalApicAddress
 * Retrieve the local apic address from the mp tables. The mp tables must be initialized
 * with MpInitialize */
KERNELAPI
OsStatus_t
KERNELABI
MpGetLocalApicAddress(
    _Out_ uintptr_t*    Address)
{
    // Sanitize state
    if (MpHeader.Signature != MP_HEADER_MAGIC) {
        return OsError;
    }
    *Address = (uintptr_t)MpConfigurationTable.LocalApicAddress;
    return OsSuccess;
}
