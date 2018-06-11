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

#ifndef __MPTABLES_PARSER__
#define __MPTABLES_PARSER__

/* Includes
 * - Library */
#include <os/osdefs.h>

#define MP_HEADER_MAGIC                 0x5F504D5F

// MpHeader
// Primary structure that is located somewhere in the
// EBDA or the BIOS memory space, and is marked by
// signature value MP_HEADER_MAGIC
PACKED_TYPESTRUCT(MpHeader, {
    uint32_t            Signature;
    uint32_t            ConfigurationTableAddress;
    uint8_t             Length;
    uint8_t             Revision;
    uint8_t             DefaultConfiguration; // If non zero ignore ConfigurationTableAddress
    uint32_t            Features;
});
#define MP_FEATURE_IMCR                 0x80

// MpConfigurationTable
// Contains configuration data about the pc
PACKED_TYPESTRUCT(MpConfigurationTable, {
    uint32_t            Signature; //"PCMP"
    uint16_t            Length;
    uint8_t             Revision;
    uint8_t             Checksum;
    uint8_t             OemId[8];
    uint8_t             ProductId[12];
    uint32_t            OemTable;
    uint16_t            OemTableLength;
    uint16_t            EntryCount;
    uint32_t            LocalApicAddress;
    uint16_t            ExtendedTableLength;
    uint8_t             ExtendedTableChecksum;
    uint8_t             Reserved;
});

/* MpInitialize
 * Searches known memory places where the mp-tables can exist, returns 
 * OsSuccess if they exist - otherwise OsError */
KERNELAPI OsStatus_t KERNELABI
MpInitialize(void);

/* MpGetLocalApicAddress
 * Retrieve the local apic address from the mp tables. The mp tables must be initialized
 * with MpInitialize */
KERNELAPI OsStatus_t KERNELABI
MpGetLocalApicAddress(
    _Out_ uintptr_t*    Address);

#endif //!__MPTABLES_PARSER__
