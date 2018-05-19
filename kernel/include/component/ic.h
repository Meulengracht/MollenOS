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
 * - The Interrupt Controller component. This component has the task
 *   of mapping and managing interrupts in the domain
 */

#ifndef __COMPONENT_INTERRUPT_CONTROLLER__
#define __COMPONENT_INTERRUPT_CONTROLLER__

/* Includes
 * - System */
#include <os/osdefs.h>

typedef struct _SystemInterruptController {
    int     Id;
    int     NumberOfInterruptLines;
} SystemInterruptController_t;

#endif // !__COMPONENT_INTERRUPT_CONTROLLER__
