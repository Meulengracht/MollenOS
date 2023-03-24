/**
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
 * - Writes the C string pointed by str to the stream.
 * - The function begins copying from the address specified (str) until it 
 *   reaches the terminating null character ('\0'). 
 *   This terminating null-character is not copied to the stream.
 */

#include <wchar.h>
#include <string.h>
#include <stdio.h>

int fputws(
    _In_ const wchar_t* str,
    _In_ FILE*          stream)
{
    static const wchar_t nl = L'\n';
    size_t               len = wcslen(str);
    int                  ret;

    flockfile(stream);
    if(fwrite(str, sizeof(*str), len, stream) != len) {
        funlockfile(stream);
        return EOF;
    }

    ret = fwrite(&nl,sizeof(nl),1,stream) == 1 ? 0 : EOF;
    funlockfile(stream);
    return ret;
}
