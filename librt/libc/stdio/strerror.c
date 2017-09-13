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
 * MollenOS C Library
 */

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* The strerror
 * converts an error code
 * to a string representation */
char *strerror(int errnum)
{
	/* Sanitize upper bound */
	if (errnum >= _MAX_ERRNO)
		return (char*)_errstrings[126];

	/* Sanitize lower bound */
	if (errnum < 0)
		return (char*)_errstrings[0];

	/* Yay */
	return (char*)_errstrings[errnum];
}