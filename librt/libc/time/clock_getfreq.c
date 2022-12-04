/**
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
 */

#include <os/time.h>
#include <time.h>

static UInteger64_t g_clockFrequency = { 0 };

clock_t
clock_getfreq(void)
{
    if (g_clockFrequency.u.LowPart != 0) {
        return (clock_t)g_clockFrequency.QuadPart;
    }

    OSGetClockFrequency(OSClockSource_MONOTONIC, &g_clockFrequency);
    return (clock_t)g_clockFrequency.QuadPart;
}
