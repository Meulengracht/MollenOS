/**
 * MollenOS
 *
 * Copyright 2023, Philip Meulengracht
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

#include <errno.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

int asprintf(
	_In_ char**      ret,
	_In_ const char* format,
	...)
{
	char    buffer[512];
	int     result;
	FILE    stream = { 0 };
	va_list argptr;

	if (format == NULL || ret == NULL) {
        errno = EINVAL;
		return -1;
	}

	memset(&buffer[0], 0, sizeof(buffer));
    __FILE_Streamout(&stream, &buffer[0], sizeof(buffer));

	va_start(argptr, format);
    result = streamout(&stream, format, argptr);
	va_end(argptr);

	*ret = strdup(&buffer[0]);
	return result;
}
