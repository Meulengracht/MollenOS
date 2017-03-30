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
 * MollenOS MCore - Timer Support Definitions & Structures
 * - This header describes the base timer-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _TIMERS_INTERFACE_H_
#define _TIMERS_INTERFACE_H_

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Timer callback definition, the
 * function proto-type for a timer callback */
typedef void(*TimerHandler_t)(void*);

/* Timer information flags on creation, 
 * allows customization and timer settings */
#define	TIMER_PERIODIC				0x1
#define TIMER_FAST					0x2

#endif //!_TIMERS_INTERFACE_H_
