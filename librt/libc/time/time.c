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
 * MollenOS C Library - Get Time
 */

/* Includes 
 * - Library */
#include <os/mollenos.h>
#include <time.h>
#include <stddef.h>
#include <string.h>

/* Time
 * Gets the time from either a 
 * time seed or returns the current time */
time_t
time(time_t *timer)
{
	// Variables
	struct tm TimeStruct;
	time_t Converted = 0;

	// Check if a system clock is available
	if (SystemTime(&TimeStruct) != OsSuccess) {
		return 0;
	}

	// Now convert the sys-time to time_t
	Converted = mktime(&TimeStruct);
	if (timer != NULL) {
		*timer = Converted;
	}
	return Converted;
}
