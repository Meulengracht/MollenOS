/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * ACPI(CA) Table Enumeration Interface
 */

#define __MODULE "ACPI"
#define __TRACE

#include <arch/acpi.h>
#include <acpiinterface.h>
#include <debug.h>

// Static storage for acpi states
static int g_acpiStatus       = ACPI_NOT_AVAILABLE;
AcpiEcdt_t EmbeddedController = { 0 };

oserr_t
AcpiInitializeEarly(void)
{
    ACPI_TABLE_HEADER* header;
    oserr_t            osStatus;
    ACPI_STATUS        acpiStatus;
    TRACE("AcpiInitializeEarly()");

    // MachineMemoryInitialize must have completed: ACPICA allocates storage
    // and its OSL maps table addresses through the kernel memory manager.
    // This is the single boot-time initialization, before namespace loading.
    // Clear availability until both subsystem and table setup have succeeded.
    g_acpiStatus = ACPI_NOT_AVAILABLE;

    // Perform the initial setup of ACPICA
    acpiStatus = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("AcpiInitializeEarly acpi is not available (acpi disabled %" PRIuIN ")", acpiStatus);
        g_acpiStatus = ACPI_NOT_AVAILABLE;
        return OS_EUNKNOWN;
    }

    // Do the early table enumeration
    acpiStatus = AcpiInitializeTables(NULL, ACPI_MAX_INIT_TABLES, TRUE);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("AcpiInitializeEarly failed to obtain acpi tables (acpi disabled %" PRIuIN ")", acpiStatus);
        g_acpiStatus = ACPI_NOT_AVAILABLE;
        return OS_EUNKNOWN;
    }

    // Availability means tables can be queried, not that the hardware is ready.
    // Publish it before the architecture hook so its table consumers can use
    // the same availability checks as later firmware queries. A hook failure
    // leaves the table manager usable for diagnostics and returns to the caller.
    g_acpiStatus = ACPI_AVAILABLE;
    osStatus = ArchAcpiInitialize();

    // Check for ECDT presence and enumerate. This table is not present on
    // any of the modern systems, they instead appear in the acpi namespace
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_ECDT, 0, &header))) {
        ACPI_TABLE_ECDT* ecdtTable = (ACPI_TABLE_ECDT*)header;
        TRACE("AcpiInitializeEarly Enumerating the ECDT Table");
        TODO("missing implementation for ACPI ECDT parsing");
        //AcpiEnumerateECDT((void*)((uintptr_t)EcdtTable + sizeof(ACPI_TABLE_ECDT)),
        //    (void*)((uintptr_t)EcdtTable + EcdtTable->Header.Length));
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(header);
    }

    // Check for SBST presence and enumerate
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SBST, 0, &header))) {
        ACPI_TABLE_SBST* sbspTable = (ACPI_TABLE_SBST*)header;
        TRACE("AcpiInitializeEarly Parsing the SBST Table");
        TODO("missing implementation for ACPI SBST parsing");
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(header);
    }
    return osStatus;
}

int AcpiAvailable(void)
{
    return g_acpiStatus;
}
