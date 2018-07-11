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

#include <crc32.h>

// Static storage for the crc-table
static uint32_t CrcTable[256] = { 0 };

/* Crc32GenerateTable
 * Generates a dynamic crc-32 table. */
void
Crc32GenerateTable(void)
{
    register uint32_t CrcAccumulator;
    register int i, j;

    for (i=0;  i < 256; i++) {
        CrcAccumulator = ((uint32_t) i << 24);
        for (j = 0;  j < 8;  j++) {
            if (CrcAccumulator & 0x80000000L) {
                CrcAccumulator = (CrcAccumulator << 1) ^ POLYNOMIAL;
            }
            else {
                CrcAccumulator = (CrcAccumulator << 1);
            }
        }
        CrcTable[i] = CrcAccumulator;
    }
}

/* Crc32Generate
 * Generates an crc-32 checksum from the given accumulator and
 * the given data. */
uint32_t
Crc32Generate(
    _In_ uint32_t CrcAccumulator, 
    _In_ uint8_t *DataPointer, 
    _In_ size_t DataSize)
{
    register size_t i, j;

    for (j = 0; j < DataSize; j++) {
        i = ((int) (CrcAccumulator >> 24) ^ *DataPointer++) & 0xFF;
        CrcAccumulator = (CrcAccumulator << 8) ^ CrcTable[i];
    }
    CrcAccumulator = ~CrcAccumulator;
    return CrcAccumulator;
}
