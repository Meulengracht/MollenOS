/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS System Component Infrastructure 
 * - The Central Processing Unit Component is one of the primary actors
 *   in a domain. 
 */

#ifndef __COMPONENT_CPU__
#define __COMPONENT_CPU__

/* Includes
 * - System */
#include <os/osdefs.h>

typedef struct _SystemCpu {
    int         NumberOfCores;
} SystemCpu_t;

#endif // !__COMPONENT_CPU__
