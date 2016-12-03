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
* MollenOS C Library - String Library
*/

/* Includes */
#include <os/Thread.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

/*
 *     STRING
 *     Searching
 */
char* strtok_r(char* s, const char* delimiters, char** lasts)
{
	char *sbegin, *send;
     sbegin = s ? s : *lasts;
     sbegin += strspn(sbegin, delimiters);
     if (*sbegin == '\0') {
         *lasts = "";
         return NULL;
     }
     send = sbegin + strcspn(sbegin, delimiters);
     if (*send != '\0')
         *send++ = '\0';
     *lasts = send;
     return sbegin;
}

/* Actual wrapper call */
char* strtok(char* str, const char* delimiters)
{
	/* Call with the pointer in TLS 
	 * so strktok is thread specific */
	return strtok_r(str, delimiters, &(TLSGetCurrent()->StrTokNext));
}