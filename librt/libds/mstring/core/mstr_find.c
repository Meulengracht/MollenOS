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
 *
 */

#include "../common/private.h"

int mstr_find_u8(mstring_t* string, const char* u8, int startIndex)
{
    int    u8i = 0;
    size_t i   = 0;
    mchar_t needle;

    if (string == NULL || string->__length == 0) {
        return -1;
    }

    // to speed up things, we look for the initial character before
    // making any compare calls.
    needle = mstr_next(u8, &u8i);
    while (i < string->__length) {
        if (needle == string->__data[i] && (int)i >= startIndex) {
            // compare the entire u8 sequence
            if (!mstr_cmp_u8_index(string, u8, i)) {
                return (int)i;
            }
        }
        i++;
    }
    return -1;
}

int mstr_rfind_u8(mstring_t* string, const char* u8, int startIndex)
{
    int     u8i = 0;
    size_t  i   = 0;
    mchar_t needle;
    int     lastOccurrence  = -1;

    if (string == NULL || string->__length == 0) {
        return -1;
    }

    // to speed up things, we look for the initial character before
    // making any compare calls.
    needle = mstr_next(u8, &u8i);
    while (i < string->__length) {
        if (i >= (size_t)startIndex) {
            break;
        }

        if (needle == string->__data[i] && i < (size_t)startIndex) {
            // compare the entire u8 sequence
            if (!mstr_cmp_u8_index(string, u8, i)) {
                lastOccurrence = (int)i;
            }
        }
        i++;
    }
    return lastOccurrence;
}
