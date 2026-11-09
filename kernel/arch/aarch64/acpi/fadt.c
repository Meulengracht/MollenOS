/**
 * Copyright, Philip Meulengracht
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
 */

#include "tables.h"

oserr_t
Arm64AcpiReadFadt(
    _InOut_ struct Arm64Platform* platform)
{
    ACPI_TABLE_HEADER* header;
    ACPI_TABLE_FADT*   table;
    oserr_t            status;

    // The ARM boot flags and the minor revision were both added in FADT 5.1.
    // So the table must be long enough to hold them before we read either
    // field. Fields added by newer revisions are not used here, and they
    // don't have to match the ACPICA structure we build against.
    status = Arm64AcpiGetTable(
        ACPI_SIG_FADT,
        offsetof(ACPI_TABLE_FADT, MinorRevision) + sizeof(UINT8),
        &header
    );
    if (status != OS_EOK) {
        return status;
    }

    table = (ACPI_TABLE_FADT*)header;
    if (header->Revision < 5 || (header->Revision == 5 && table->MinorRevision < 1)) {
        status = OS_ENOTSUPPORTED;
        goto exit;
    }

    if (!(table->Flags & ACPI_FADT_HW_REDUCED) || !(table->ArmBootFlags & ACPI_FADT_PSCI_COMPLIANT)) {
        status = OS_ENOTSUPPORTED;
        goto exit;
    }

    // There is no fallback to the ARM parking protocol. Every secondary CPU
    // we register is started with PSCI CPU_ON, and firmware decides whether
    // that goes through SMC or HVC. We never guess it from the exception level
    // we were entered at, or from the CPU model. Nothing calls PSCI here.
    // Secondary CPUs are started later, once interrupts and timers are up.
    platform->PsciConduit = (table->ArmBootFlags & ACPI_FADT_PSCI_USE_HVC) ? VBOOT_PSCI_HVC : VBOOT_PSCI_SMC;

exit:
    AcpiPutTable(header);
    return status;
}
