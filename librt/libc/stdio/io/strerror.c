/**
 * Copyright 2016, Philip Meulengracht
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
 *
 * errno error string
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>

// important to declare as an array and not as a double pointer, it affects
// how data is loaded
extern const char* g_errnoStrings[];

char* strerror(int errnum)
{
	if (errnum >= _MAX_ERRNO)
		return (char*)g_errnoStrings[126];
	if (errnum < 0)
		return (char*)g_errnoStrings[0];
	return (char*)g_errnoStrings[errnum];
}
