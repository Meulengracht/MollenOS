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
 * Gracht Debug Type Definitions & Structures
 * - This header describes the base debug-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_DEBUG_H__
#define __GRACHT_DEBUG_H__

#if defined(MOLLENOS)

//#define __TRACE
#include <ddk/utils.h>

#elif defined(__linux__)
#include <stdio.h>

#define TRACE(...)   printf(__VA_ARGS__)
#define WARNING(...) printf(__VA_ARGS__)
#define ERROR(...)   printf(__VA_ARGS__)

#else
#error "Undefined platform for aio"
#endif

#endif // !__GRACHT_AIO_H__
