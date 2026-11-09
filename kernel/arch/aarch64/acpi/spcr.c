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

#define __MODULE "ACPI-SPCR"

#include <acpi.h>
#include <arch/output.h>
#include <debug.h>
#include <log.h>
#include <string.h>
#include "../common/private.h"
#include "spcr.h"
#include "tables.h"

static oserr_t
__ValidateSpcr(
    _In_ const ACPI_TABLE_SPCR* table)
{
    oserr_t status;

    // The shared check covers the signature, the length and the checksum of
    // the whole table. Revision 2 added the PL011 interface type, so we need
    // at least that. Newer revisions only add to the table, and the register
    // description we use here stays the same.
    status = Arm64AcpiValidateTable(
        &table->Header,
        ACPI_SIG_SPCR,
        sizeof(ACPI_TABLE_SPCR)
    );
    if (status != OS_EOK) {
        return status;
    }
    if (table->Header.Revision < 2) {
        return OS_ENOTSUPPORTED;
    }
    if (!table->SerialPort.Address) {
        return OS_ENOENT;
    }

    if (table->InterfaceType != ACPI_DBG2_ARM_PL011) {
        return OS_ENOTSUPPORTED;
    }

    // The serial driver only does aligned 32-bit accesses at the fixed PL011
    // register offsets. If the access width is left unspecified, the interface
    // type decides it. Any other width, or a register described as a bit
    // field, is not supported.
    if (table->SerialPort.SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY || table->SerialPort.BitOffset != 0) {
        return OS_ENOTSUPPORTED;
    }

    if (table->SerialPort.AccessWidth != 0 && table->SerialPort.AccessWidth != 3) {
        return OS_ENOTSUPPORTED;
    }
    
    if (table->SerialPort.BitWidth != 0 && table->SerialPort.BitWidth != 32) {
        return OS_ENOTSUPPORTED;
    }

    if (table->SerialPort.Address % ARM64_MMIO_REGISTER_SIZE) {
        return OS_EINVALPARAMS;
    }

    // The console only polls TXFF and writes DR, and it doesn't use the UART
    // interrupt. DCD and software flow control need receive and modem
    // handling, which we don't have. Hardware RTS/CTS is fine, as the PL011
    // keeps using the settings firmware left on it.
    if (table->FlowControl & ~2U) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

void
Arm64AcpiInitializeConsole(void)
{
    ACPI_TABLE_HEADER* header;
    ACPI_TABLE_SPCR*   table;
    ACPI_STATUS        acpiStatus;
    oserr_t            status;
    uint64_t           address = 0;

    acpiStatus = AcpiGetTable(ACPI_SIG_SPCR, 1, &header);
    if (ACPI_FAILURE(acpiStatus)) {
        if (acpiStatus != AE_NOT_FOUND) {
            WARNING("Cannot read SPCR: %u", acpiStatus);
        }
        return;
    }

    table = (ACPI_TABLE_SPCR*)header;
    status = __ValidateSpcr(table);
    if (status == OS_EOK) {
        address = table->SerialPort.Address;
    }
    AcpiPutTable(header);
    
    if (status != OS_EOK) {
        WARNING("SPCR console unavailable: %u", status);
        return;
    }

    g_arm64Platform.Uart = address;
    g_arm64Platform.UartLength = ARM64_UART_REGION_MINIMUM;
    
    status = SerialPortInitialize();
    if (status != OS_EOK) {
        g_arm64Platform.Uart = 0;
        g_arm64Platform.UartLength = 0;
        WARNING("Cannot map SPCR console: %u", status);
        return;
    }

    // ConsoleInitialize ran before ACPI told us where the UART is. Turn on
    // rendering now, so the startup messages buffered so far are written out
    // to this console.
    LogSetRenderMode(1);
    DEBUG("SPCR PL011 console at 0x%llx", (unsigned long long)address);
}
