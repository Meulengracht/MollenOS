/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <os/time.h>
#include <stddef.h>
#include <time.h>
#include "local.h"

time_t time(time_t* tim)
{
    OSTimestamp_t timeValue;
    time_t        converted = 0;

	if (OSGetTime(OSTimeSource_UTC, &timeValue) == OS_EOK) {
        // time is expected to return a time since the epoch of Jaunary 1, 1970
        // but the time epoch in Vali is January 1, 2000. So we fix this by adding
        // the below difference which is the exact number of seconds between those
        // dates
        converted = (time_t)(timeValue.Seconds + EPOCH_DIFFERENCE);

        // TODO time-zone support?
        if (tim != NULL) {
            *tim = converted;
        }
	}
	return converted;
}
