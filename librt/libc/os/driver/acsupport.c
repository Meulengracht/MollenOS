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

/* Includes
 * - System */
#include <os/driver/acpi.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stdlib.h>

/* AcpiQueryStatus
 * Queries basic acpi information and returns either OsSuccess
 * or OsError if Acpi is not supported on the running platform */
OsStatus_t
AcpiQueryStatus(
    AcpiDescriptor_t *AcpiDescriptor) {
	return Syscall_AcpiQuery(AcpiDescriptor);
}

/* AcpiQueryTable
 * Queries the full table information of the table that matches
 * the given signature, and copies the information to the supplied pointer
 * the buffer is automatically allocated, and should be cleaned up afterwards  */
OsStatus_t AcpiQueryTable(const char *Signature, ACPI_TABLE_HEADER **Table)
{
	/* We need this temporary storage */
	ACPI_TABLE_HEADER Header;
	OsStatus_t Result;

	/* Now query for the header information
	 * so we know what we should allocate */
	Result = Syscall_AcpiGetHeader(Signature, &Header);

	/* Sanitize the result */
	if (Result != OsSuccess) {
		return Result;
	}

	/* Ok, now we can allocate a buffer able to contain
	 * the entire table information */
	*Table = (ACPI_TABLE_HEADER*)malloc(Header.Length);

	/* And finally, we can query for the entirety of
	 * the requested table! */
	return Syscall_AcpiGetTable(Signature, *Table);
}

/* AcpiQueryInterrupt
 * Queries the interrupt-line for the given bus, device and
 * pin combination. The pin must be zero indexed. Conform flags
 * are returned in the <AcpiConform> */
OsStatus_t AcpiQueryInterrupt(DevInfo_t Bus, DevInfo_t Device, int Pin,
	int *Interrupt, Flags_t *AcpiConform)
{
	// Validate the pointers
	if (Interrupt == NULL || AcpiConform == NULL) {
		return OsError;
	}
	return Syscall_AcpiQueryInterrupt(Bus, Device, Pin, Interrupt, AcpiConform);
}
