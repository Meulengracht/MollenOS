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

#include "internal/_file.h"
#include "stdio.h"

int getw(
    _In_ FILE *file)
{
    char*        ch;
    int          i, k;
    unsigned int j;

    ch = (char*)&i;

    flockfile(file);
    for (j = 0; j < sizeof(int); j++) {
        k = fgetc(file);
        if (k == EOF) {
            file->Flags |= _IOEOF;
            funlockfile(file);
            return EOF;
        }
        ch[j] = (char)k;
    }
    funlockfile(file);
    return i;
}
