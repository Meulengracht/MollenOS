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
 * - Reads characters from the standard input (stdin) and 
 *   stores them as a C string into str until a newline character or 
 *   the end-of-file is reached. 
 * - The newline character, if found, is not copied into str.
 */

#include <wchar.h>
#include <stdio.h>
 
wchar_t* getws(
    _In_ wchar_t* buf)
{
    wint_t cc;
    wchar_t* ws = buf;

    _lock_file(stdin);
    for (cc = fgetwc(stdin); cc != WEOF && cc != '\n'; cc = fgetwc(stdin)) {
        if (cc != '\r')
            *buf++ = (wchar_t)cc;
    }
    *buf = '\0';

    _unlock_file(stdin);
    return ws;
}
 