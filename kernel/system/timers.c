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

/* Includes */
#include <arch.h>
#include <timers.h>
#include <list.h>

/* Globals */
list_t *glb_timers = NULL;
volatile tmid_t glb_timer_ids = 0;
volatile uint32_t glb_timers_initialized = 0;

/* Init */
void timers_init(void)
{
	/* Create list */
	glb_timers = list_create(LIST_SAFE);
	glb_timers_initialized = 0xDEADBEEF;
	glb_timer_ids = 0;
}

tmid_t timers_create_periodic(timer_handler_t callback, uint32_t ms)
{
	_CRT_UNUSED(callback);
	_CRT_UNUSED(ms);

	/* Sanity */
	if (glb_timers_initialized != 0xDEADBEEF)
		timers_init();

	return glb_timer_ids;
}