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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - String Support Definitions & Structures
 * - This file describes the base string-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_tls.h>
#include <string.h>
#include <stddef.h>

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
    if (*send != '\0') {
        *send++ = '\0';
    }

    *lasts = send;
    return sbegin;
}

char* strtok(char* str, const char* delimiters)
{
	return strtok_r(str, delimiters, &(__tls_current()->strtok_next));
}
