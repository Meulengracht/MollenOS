/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Gracht CRC Type Definitions & Structures
 * - This header describes the base crc-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CRC_H__
#define __GRACHT_CRC_H__

#include "types.h"

// CRC API
// General crc routines for providing data integrity
uint16_t crc16_generate(const unsigned char* data, size_t length);
uint32_t crc32_generate(const unsigned char *input_str, size_t num_bytes);

#endif // !__GRACHT_CRC_H__
