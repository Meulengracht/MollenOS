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
 * MollenOS MCore - CRC Implementation
 *  - Implemented using lookup tables for speed
 */

#ifndef __MCORE_CRC32_H__
#define __MCORE_CRC32_H__

/* Includes
 * - Library */
#include <os/osdefs.h>

// Standard CRC-32 polynomial
#define POLYNOMIAL              0x04c11db7L

/* Crc32GenerateTable
 * Generates a dynamic crc-32 table. */
KERNELAPI
void
KERNELABI
Crc32GenerateTable(void);

/* Crc32Generate
 * Generates an crc-32 checksum from the given accumulator and
 * the given data. */
KERNELAPI
uint32_t
KERNELABI
Crc32Generate(
    _In_ uint32_t CrcAccumulator, 
    _In_ uint8_t *DataPointer, 
    _In_ size_t DataSize);

#endif //!__MCORE_CRC32_H__
