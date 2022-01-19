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

#ifndef __CRC_H__
#define __CRC_H__

#include <stdint.h>
#include <stddef.h>

#define CRC_BEGIN  0xFFFFFFFF
#define POLYNOMIAL 0x04c11db7L      // Standard CRC-32 ppolynomial

/**
 * @brief 
 * 
 */
extern void
crc_init(void);

/**
 * @brief 
 * 
 * @param accumulator 
 * @param data 
 * @param length 
 * @return uint32_t 
 */
extern uint32_t
crc_calculate(
    uint32_t accumulator, 
    uint8_t* data, 
    size_t   length);

#endif //!__CRC_H__
