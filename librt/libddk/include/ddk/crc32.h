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

#ifndef __DDK_CRC32_H__
#define __DDK_CRC32_H__

#include <ddk/ddkdefs.h>

/**
 * @brief Pre-generates the CRC32 data table that must be called once before using
 * the Crc32Calculate function.
 */
DDKDECL(void, Crc32GenerateTable(void));

/**
 * @brief Generates a new crc32 value from the data, given the accumulator.
 *
 * @param crcAccumulator [In] The value that should be used as the inital values for the crc.
 * @param data           [In] A pointer to the data.
 * @param length         [In] Length of the data.
 * @return               A new crc value.
 */
DDKDECL(uint32_t, Crc32Calculate(
    _In_ uint32_t       crcAccumulator,
    _In_ const uint8_t* data,
    _In_ size_t         length));

#endif //!__VALI_CRC32_H__
