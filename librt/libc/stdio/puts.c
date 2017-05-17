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
* MollenOS C Library - Put String
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* The puts */
int puts(const char *sstr)
{
	/* Vars for iteration */
	int i = 0;

	/* Sanity */
	if (sstr == NULL) {
		return 0;
	}

	/* Loop while valid */
	while (sstr[i]) {
		if (putchar(sstr[i]) == EOF) {
			return EOF;
		}

		/* Increase */
		i++;
	}

	/* Output newline */
	if (putchar('\n') == EOF) {
		return EOF;
	}

	/* Specs require non-negative */
	return 1;
}