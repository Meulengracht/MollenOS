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
#include <heap.h>
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

tmid_t timers_create_periodic(timer_handler_t callback, void *arg, uint32_t ms)
{
	timer_t *timer;
	tmid_t id;

	/* Sanity */
	if (glb_timers_initialized != 0xDEADBEEF)
		timers_init();

	/* Allocate */
	timer = (timer_t*)kmalloc(sizeof(timer_t));
	timer->callback = callback;
	timer->argument = arg;
	timer->ms = ms;
	timer->ms_left = ms;

	/* Append to list */
	list_append(glb_timers, list_create_node(glb_timer_ids, timer));

	/* Increase */
	id = glb_timer_ids;
	glb_timer_ids++;

	return id;
}

/* This should be called by only ONE periodic irq */
void timers_apply_time(uint32_t ms)
{
	foreach(i, glb_timers)
	{
		timer_t *timer = (timer_t*)i->data;
		timer->ms_left -= ms;

		/* Pop timer? */
		if (timer->ms_left <= 0)
		{
			timer->callback(timer->argument);
			timer->ms_left = (int32_t)timer->ms;
		}
	}
}