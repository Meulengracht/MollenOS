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
 * - Writes the wide C string pointed by str to the standard output 
 *   (stdout) and appends a newline character ('\n').
 */

#include <internal/_io.h>
#include <string.h>
#include <stdio.h>

int puts(
    _In_ __CONST char *s)
{
    size_t len = strlen(s);
    int    ret;

    _lock_stream(stdout);
    if (fwrite(s, sizeof(*s), len, stdout) != len) {
        _unlock_stream(stdout);
        return EOF;
    }

    ret = fwrite("\n",1,1,stdout) == 1 ? 0 : EOF;
    _unlock_stream(stdout);
    return ret;
}
