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
* MollenOS Threading Scheduler
* Implements scheduling with priority
* Priority 61 is System Priority.
* Priority 60 - 0 are Normal Priorties
* Priorities 60 - 0 start at 10 ms, slowly increases to 300 ms.
* Priority boosts every 1000 ms?
* On yields, keep priority.
* On task-switchs, decrease priority.
* A thread can only stay a maximum in each priority.
*/

#ifndef _MCORE_SCHEDULER_H_
#define _MCORE_SCHEDULER_H_

/* Includes */
#include <arch.h>
#include <crtdefs.h>
#include <list.h>

/* Definitions */
#define MCORE_SCHEDULER_LEVELS		61
#define MCORE_INITIAL_TIMESLICE		10
#define MCORE_SCHEDULER_BOOST_MS	7000

/* Prototypes */
_CRT_EXTERN void scheduler_init(void);
_CRT_EXTERN void scheduler_ready_thread(list_node_t* node);
_CRT_EXTERN list_node_t *scheduler_schedule(cpu_t cpu, list_node_t *node, int preemptive);

#endif // !_MCORE_SCHEDULER_H_
