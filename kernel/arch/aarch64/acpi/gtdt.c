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

#include <ddk/types/interrupt.h>
#include "tables.h"

oserr_t
Arm64AcpiReadGtdt(
    _InOut_ struct Arm64Platform* platform)
{
    ACPI_TABLE_HEADER* header;
    ACPI_TABLE_GTDT*   table;
    unsigned int       flags;
    oserr_t            status;

    // The scheduler only needs the per-CPU physical timer to start. We ignore
    // the MMIO counter blocks, the virtual and EL2 timers, and the watchdog
    // subtables. The counter frequency is still read from CNTFRQ_EL0.
    status = Arm64AcpiGetTable(
        ACPI_SIG_GTDT,
        offsetof(ACPI_TABLE_GTDT, NonSecureEl1Flags) + sizeof(UINT32),
        &header
    );
    if (status != OS_EOK) {
        return status;
    }

    table = (ACPI_TABLE_GTDT*)header;
    if (header->Revision < 2) {
        status = OS_ENOTSUPPORTED;
        goto exit;
    }

    if (table->NonSecureEl1Interrupt < ARM64_GIC_PPI_BASE || table->NonSecureEl1Interrupt >= ARM64_GIC_SPI_BASE) {
        status = OS_ENOTSUPPORTED;
        goto exit;
    }

    if (table->NonSecureEl1Flags & ACPI_GTDT_INTERRUPT_MODE) {
        status = OS_ENOTSUPPORTED;
        goto exit;
    }

    // GTDT gives the absolute interrupt ID, where DT gives an index relative
    // to the PPI base. So the ID is used as is, and the PPI base must not be
    // added again. The GTDT flag bits are also laid out differently, so they
    // are converted to the kernel's own interrupt flags here.
    flags = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_TRIGGERMODE;
    if (table->NonSecureEl1Flags & ACPI_GTDT_INTERRUPT_POLARITY) {
        flags |= INTERRUPT_ACPICONFORM_POLARITY;
    }

    platform->TimerInterrupt = table->NonSecureEl1Interrupt;
    platform->TimerFlags = flags;

exit:
    AcpiPutTable(header);
    return status;
}
