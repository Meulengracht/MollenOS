/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 */

#include "internal/_tls.h"
#include "stdio.h"
#include "stdlib.h"
#include "time.h"

static char* g_chars = "ABCDEFGHIJKLMNOPQRSTUVWXZY";
static char* g_nums  = "0123456789";

char* tmpnam(
        _In_ char* buffer)
{
    char* out = buffer;
    int   i;

    if (!out) {
        out = &__tls_current()->tmpname_buffer[0];
    }

    srand(clock());
    for (i = 0; i < L_tmpnam - 1; i++) {
        if (i & 1) {
            out[i] = g_chars[rand() % 26];
        }
        else {
            out[i] = g_nums[rand() % 10];
        }
    }
    out[L_tmpnam] = 0;

    return out;
}
