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
 * MollenOS MCore - Support Definitions & Structures
 * - This header describes the base structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBC_DRIVER_PRIVATE__
#define __LIBC_DRIVER_PRIVATE__

/* Includes
 * - Library */
#include <os/osdefs.h>

/* BufferObject Structure (Private)
 * This is the way to interact with transfer
 * buffers throughout the system, must be used
 * for any hardware transactions */
typedef struct _BufferObject {
    const char*         Virtual;
    uintptr_t           Physical;
    size_t              Length;
    size_t              Capacity;
    size_t              Position;
} BufferObject_t;

#endif //!__LIBC_DRIVER_PRIVATE__
