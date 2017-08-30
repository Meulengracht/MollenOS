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
 * MollenOS - C Standard Library
 * - Writes a string to the stream and advances the position indicator.
 */

#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

wchar_t *fgetws(
    _In_ wchar_t *s,
    _In_ int size,
    _In_ FILE *file)
{
    int cc = WEOF;
    wchar_t *buf_start = s;

    _lock_file(file);

    while ((size > 1) && (cc = fgetwc(file)) != WEOF && cc != '\n')
    {
        *s++ = (char)cc;
        size--;
    }
    if ((cc == WEOF) && (s == buf_start)) /* If nothing read, return 0*/
    {
        _unlock_file(file);
        return NULL;
    }
    if ((cc != WEOF) && (size > 1))
        *s++ = cc;
    *s = 0;

    _unlock_file(file);
    return buf_start;
}
