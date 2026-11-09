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

#ifndef __DDK_PCI_MCFG_H__
#define __DDK_PCI_MCFG_H__

#include <ddk/acpi.h>
#include <string.h>

/**
 * @brief Validates the whole MCFG table. This must be done before the table is
 * used for PCI configuration access, either by AML or by deviced.
 *
 * @param header The table bytes, including the standard ACPI header.
 * @param length The number of bytes that can be read from header.
 * @return OS_EOK if the table has at least one ECAM allocation, and all
 *         allocations are 1 MiB aligned and do not overlap each other.
 */
static inline oserr_t
__PciValidateMcfg(
    _In_ const ACPI_TABLE_HEADER* header,
    _In_ size_t                   length)
{
    const ACPI_MCFG_ALLOCATION* entries;
    const ACPI_MCFG_ALLOCATION* entry;
    uint64_t                   start;
    uint64_t                   end;
    uint64_t                   otherStart;
    uint64_t                   otherEnd;
    uint8_t                    checksum = 0;
    size_t                     count;

    if (length < sizeof(ACPI_TABLE_MCFG) || header->Length != length ||
        memcmp(header->Signature, ACPI_SIG_MCFG, 4)) {
        return OS_EINVALPARAMS;
    }

    if ((length - sizeof(ACPI_TABLE_MCFG)) % sizeof(ACPI_MCFG_ALLOCATION)) {
        return OS_EINVALPARAMS;
    }

    for (size_t i = 0; i < length; i++) {
        checksum += ((const uint8_t*)header)[i];
    }
    if (checksum) {
        return OS_EINVALPARAMS;
    }

    entries = (const void*)((const uint8_t*)header + sizeof(ACPI_TABLE_MCFG));
    count = (length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
    if (!count) {
        return OS_ENOENT;
    }

    for (size_t i = 0; i < count; i++) {
        entry = &entries[i];
        if (entry->StartBusNumber > entry->EndBusNumber ||
            !entry->Address || (entry->Address & ((1ULL << 20) - 1))) {
            return OS_EINVALPARAMS;
        }

        // The MCFG base address always points to bus 0, even if
        // StartBusNumber is not 0.
        end = ((uint64_t)entry->EndBusNumber + 1) << 20;
        if (end - 1 > UINT64_MAX - entry->Address) {
            return OS_EOVERFLOW;
        }

        start = entry->Address + ((uint64_t)entry->StartBusNumber << 20);
        end = entry->Address + end - 1;
        for (size_t j = 0; j < i; j++) {
            if (entry->PciSegment == entries[j].PciSegment &&
                entry->StartBusNumber <= entries[j].EndBusNumber &&
                entries[j].StartBusNumber <= entry->EndBusNumber) {
                return OS_EINVALPARAMS;
            }

            otherStart = entries[j].Address + ((uint64_t)entries[j].StartBusNumber << 20);
            otherEnd = entries[j].Address + (((uint64_t)entries[j].EndBusNumber + 1) << 20) - 1;

            if (start <= otherEnd && otherStart <= end) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

/**
 * @brief Finds the allocation that covers the entire bus range of a PCI root.
 * The range must fit inside a single allocation.
 *
 * @param header The MCFG table, which must have been validated with __PciValidateMcfg.
 * @param segment The PCI segment. Bus numbers are only unique within a segment.
 * @param first The first bus of the root.
 * @param last The last bus forwarded by the root.
 * @return The allocation, or NULL if no single allocation covers the bus range.
 */
static inline const ACPI_MCFG_ALLOCATION*
__PciFindMcfg(
    _In_ const ACPI_TABLE_HEADER* header,
    _In_ uint16_t                 segment,
    _In_ uint8_t                  first,
    _In_ uint8_t                  last)
{
    const ACPI_MCFG_ALLOCATION* entries;
    size_t                      count;

    entries = (const void*)((const uint8_t*)header + sizeof(ACPI_TABLE_MCFG));
    count = (header->Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(*entries);
    for (size_t i = 0; i < count; i++) {
        if (entries[i].PciSegment == segment && first >= entries[i].StartBusNumber &&
            last <= entries[i].EndBusNumber) {
            return &entries[i];
        }
    }
    return NULL;
}

#endif //!__DDK_PCI_MCFG_H__
