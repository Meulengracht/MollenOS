/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __STDIO_PRIVATE_H__
#define __STDIO_PRIVATE_H__

#include <ds/hashtable.h>
#include <internal/_io.h>

/**
 * @brief Retrieves the hashtable of IO descriptors.
 * @return A pointer to the hashtable of io-descriptors.
 */
extern hashtable_t*
IODescriptors(void);

/**
 * @brief Converts a signature to their respective implementation
 * operations.
 * @param signature The signature of the stdio operations.
 * @return A pointer to operations.
 */
stdio_ops_t*
CRTSignatureOps(
        _In_ unsigned int signature);

/**
 * @brief Parses the inheritance block and initializes any
 * io-descriptors inheritted.
 * @param inheritanceBlock A pointer to the inheritance data.
 */
extern void
CRTReadInheritanceBlock(
        _In_ void* inheritanceBlock);

#endif //!__STDIO_PRIVATE_H__
