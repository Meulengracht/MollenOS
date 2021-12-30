/**
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * CRC Implementation
 *  - Implemented using lookup tables for speed
 */

#include <ddk/crc32.h>

// Standard CRC-32 polynomial
#define POLYNOMIAL 0x04c11db7L

// Static storage for the crc-table
static uint32_t CrcTable[256] = { 0 };

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

uint32_t
Crc32Calculate(
        _In_ uint32_t       crcAccumulator,
        _In_ const uint8_t* data,
        _In_ size_t         length)
{
    register size_t i, j;

    for (j = 0; j < length; j++) {
        i = ((int) (crcAccumulator >> 24) ^ *data++) & 0xFF;
        crcAccumulator = (crcAccumulator << 8) ^ CrcTable[i];
    }
    crcAccumulator = ~crcAccumulator;
    return crcAccumulator;
}
