/**
 * Copyright 2022, Philip Meulengracht
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
 * Crc32 Implementation
 * - Contains methods for crc32 calculations to be used in the ramdisk for
 *   integrity checks.
 */

#include "crc.h"

static uint32_t g_crcTable[256] = { 0 };

void
crc_init(void)
{
    uint32_t accumulator;
    int      i, j;

    for (i = 0;  i < 256; i++) {
        accumulator = ((uint32_t) i << 24);
        for (j = 0;  j < 8;  j++) {
            if (accumulator & 0x80000000L) {
                accumulator = (accumulator << 1) ^ POLYNOMIAL;
            }
            else {
                accumulator = (accumulator << 1);
            }
        }
        g_crcTable[i] = accumulator;
    }
}

uint32_t
crc_calculate(
    uint32_t accumulator, 
    uint8_t* data, 
    size_t   length)
{
    size_t i, j;

    // Iterate each byte and accumulate crc
    for (j = 0; j < length; j++) {
        i = ((int) (accumulator >> 24) ^ *data++) & 0xFF;
        accumulator = (accumulator << 8) ^ g_crcTable[i];
    }
    accumulator = ~accumulator;
    return accumulator;
}
