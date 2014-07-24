/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCORE - Timer Manager
*/

#ifndef _MCORE_TIMERS_H_
#define _MCORE_TIMERS_H_

/* Include */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
typedef unsigned int tmid_t;
typedef void(*timer_handler_t)(void*);

/* Structures */
typedef struct _mcore_timer
{
	/* Callback */
	timer_handler_t callback;

	/* Argument for callback */
	void *argument;

	/* Periode MS */
	uint32_t ms;

	/* Counter */
	volatile int32_t ms_left;

} timer_t;

/* Prototypes */
_CRT_EXTERN tmid_t timers_create_periodic(timer_handler_t callback, void *arg, uint32_t ms);
_CRT_EXTERN void timers_apply_time(uint32_t ms);

#endif // !_MCORE_TIMERS_H_


