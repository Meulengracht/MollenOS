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

#include <string.h>

#include "tables.h"

oserr_t
Arm64AcpiValidateTable(
    _In_ const ACPI_TABLE_HEADER* header,
    _In_ const char*              signature,
    _In_ size_t                   minimumLength)
{
    const uint8_t* bytes = (const void*)header;
    uint8_t        checksum = 0;

    if (header->Length < minimumLength || memcmp(header->Signature, signature, ACPI_NAMESEG_SIZE)) {
        return OS_EINVALPARAMS;
    }

    // The checksum covers the whole table, including newer fields we don't
    // read. Tables longer than we expect are fine, but they must still be
    // intact.
    for (uint32_t i = 0; i < header->Length; i++) {
        checksum = (uint8_t)(checksum + bytes[i]);
    }
    return checksum == 0 ? OS_EOK : OS_EINVALPARAMS;
}

oserr_t
Arm64AcpiGetTable(
    _In_  const char*         signature,
    _In_  size_t              minimumLength,
    _Out_ ACPI_TABLE_HEADER** header)
{
    ACPI_STATUS acpiStatus;
    oserr_t     status;

    acpiStatus = AcpiGetTable((char*)signature, 1, header);
    if (ACPI_FAILURE(acpiStatus)) {
        return acpiStatus == AE_NOT_FOUND ? OS_ENOENT : OS_EUNKNOWN;
    }

    status = Arm64AcpiValidateTable(
        *header,
        signature,
        minimumLength
    );
    if (status != OS_EOK) {
        AcpiPutTable(*header);
        *header = NULL;
    }
    return status;
}
