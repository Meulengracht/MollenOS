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

#ifndef __ARM64_MEMORY_H__
#define __ARM64_MEMORY_H__

#include <Uefi.h>

struct Arm64FirmwareMap {
    EFI_MEMORY_DESCRIPTOR* Descriptors;
    UINTN                  Size;
    UINTN                  Stride;
};

/**
 * @brief Gets a temporary EFI memory map for firmware table validation.
 * @param Map Receives allocated descriptors and their byte stride.
 * @return EFI_SUCCESS on success. The caller frees Map->Descriptors.
 */
EFI_STATUS
Arm64ReadFirmwareMap(
    OUT struct Arm64FirmwareMap* Map);

/**
 * @brief Checks that an ACPI byte range is covered by readable ACPI memory.
 * @param Map Snapshot obtained before firmware exit.
 * @param Address Physical start of the table.
 * @param Length Nonzero table length in bytes.
 * @return TRUE for ACPI reclaim/NVS memory with no gaps or address overflow.
 */
BOOLEAN
Arm64AcpiRange(
    IN const struct Arm64FirmwareMap* Map,
    IN UINT64                         Address,
    IN UINT64                         Length);

#endif //!__ARM64_MEMORY_H__
