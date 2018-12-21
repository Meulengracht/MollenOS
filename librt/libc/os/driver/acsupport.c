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
#include <os/acpi.h>
#include <stdlib.h>

/* AcpiQueryStatus
 * Queries basic acpi information and returns either OsSuccess
 * or OsError if Acpi is not supported on the running platform */
OsStatus_t
AcpiQueryStatus(
    _In_ AcpiDescriptor_t* AcpiDescriptor)
{
    return Syscall_AcpiQuery(AcpiDescriptor);
}

/* AcpiQueryTable
 * Queries the full table information of the table that matches
 * the given signature, and copies the information to the supplied pointer
 * the buffer is automatically allocated, and should be cleaned up afterwards  */
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

/* AcpiQueryInterrupt
 * Queries the interrupt-line for the given bus, device and
 * pin combination. The pin must be zero indexed. Conform flags
 * are returned in the <AcpiConform> */
OsStatus_t AcpiQueryInterrupt(
    _In_  DevInfo_t Bus,
    _In_  DevInfo_t Device,
    _In_  int       Pin,
    _Out_ int*      Interrupt,
    _Out_ Flags_t*  AcpiConform)
{
    if (Interrupt == NULL || AcpiConform == NULL) {
        return OsError;
    }
    return Syscall_AcpiQueryInterrupt(Bus, Device, Pin, Interrupt, AcpiConform);
}
