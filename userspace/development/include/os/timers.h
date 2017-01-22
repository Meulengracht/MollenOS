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
 * MollenOS Timer Interface
 * - Contains the shared timer API that can be used by applications
 *   to create timers with callbacks
 */

#ifndef _OS_TIMERS_H_
#define _OS_TIMERS_H_

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Time definitions that can help with 
 * conversion of the different time-units */
#define FSEC_PER_NSEC				1000000L
#define NSEC_PER_MSEC				1000L
#define MSEC_PER_SEC				1000L
#define NSEC_PER_SEC				1000000000L
#define FSEC_PER_SEC				1000000000000000LL

/* Timer callback definition, the
 * function proto-type for a timer callback */
typedef void(*TimerHandler_t)(void*);

/* Timer information flags on creation, 
 * allows customization and timer settings */
#define	TIMER_PERIODIC				0x1
#define TIMER_FAST					0x2

#endif //!_OS_TIMERS_H_
