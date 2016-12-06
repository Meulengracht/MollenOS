/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS Garbage Collector
* Cleans up:
* - Threads
* - Processes
* - Heap (Not yet)
*/

#ifndef _MCORE_GARBAGECOLLECTOR_H_
#define _MCORE_GARBAGECOLLECTOR_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Initializes the gc */
__CRT_EXTERN void GcInit(void);

/* Adds garbage to the collector */
__CRT_EXTERN void GcAddWork(void);

#endif //!_MCORE_GARBAGECOLLECTOR_H_