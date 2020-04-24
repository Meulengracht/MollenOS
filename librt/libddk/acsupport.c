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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - ACPI Support Definitions & Structures
 * - This header describes the base acpi-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/acpi.h>
#include <stdlib.h>

OsStatus_t
AcpiQueryStatus(
    _In_ AcpiDescriptor_t* AcpiDescriptor)
{
    return Syscall_AcpiQuery(AcpiDescriptor);
}

OsStatus_t
AcpiQueryTable(
    _In_  const char*         Signature, 
    _Out_ ACPI_TABLE_HEADER** Table)
{
    ACPI_TABLE_HEADER Header;
    OsStatus_t        Result;
    
    Result = Syscall_AcpiGetHeader(Signature, &Header);
    if (Result != OsSuccess) {
        return Result;
    }

    *Table = (ACPI_TABLE_HEADER*)malloc(Header.Length);
    return Syscall_AcpiGetTable(Signature, *Table);
}

OsStatus_t AcpiQueryInterrupt(
    _In_  unsigned int Bus,
    _In_  unsigned int Device,
    _In_  int       Pin,
    _Out_ int*      Interrupt,
    _Out_ Flags_t*  AcpiConform)
{
    if (Interrupt == NULL || AcpiConform == NULL) {
        return OsError;
    }
    return Syscall_AcpiQueryInterrupt(Bus, Device, Pin, Interrupt, AcpiConform);
}
