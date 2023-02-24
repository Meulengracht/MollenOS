/**
 * Copyright 2022, Philip Meulengracht
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

#include "stdio.h"
#include "wchar.h"

wchar_t* fgetws(
    _In_ wchar_t* s,
    _In_ int      size,
    _In_ FILE*    file)
{
    int cc = WEOF;
    wchar_t *buf_start = s;

    while ((size > 1) && (cc = fgetwc(file)) != WEOF && cc != '\n') {
        *s++ = (wchar_t)cc;
        size--;
    }
    if ((cc == WEOF) && (s == buf_start)) { // If nothing read, return 0
        return NULL;
    }
    if ((cc != WEOF) && (size > 1))
        *s++ = cc;
    *s = 0;
    return buf_start;
}
