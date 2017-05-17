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
* MollenOS C Library - Print Error
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* The perror
 * Interprets the value of errno as 
 * an error message, and prints it to stderr */
void perror(const char * str)
{
	/* Temp format buffer */
	char TmpBuffer[256];

	/* Null it */
	memset(TmpBuffer, 0, sizeof(TmpBuffer));

	/* Sanity */
	if (str != NULL) {
		sprintf(TmpBuffer, "%s: %s\n", str, strerror(errno));
	}
	else {
		sprintf(TmpBuffer, "%s\n", strerror(errno));
	}

	/* Print */
	printf(TmpBuffer);
}