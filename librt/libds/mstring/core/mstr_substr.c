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

mstring_t* mstr_substr(mstring_t* string, int start, int length)
{
    mstring_t* substr;
    size_t     i0 = (size_t)start;
    size_t     iN = i0 + (size_t)length;
    size_t     i;

    if (string == NULL) {
        return NULL;
    }

    // Special case handlings
    // 1. Start is out of range OR length is 0
    //    - Return empty string
    // 2. End is out of range OR length is less than 0
    //    - Clamp to length, return remainder
    if (i0 >= string->__length || length == 0) {
        return mstr_new_u8("");
    }
    if (iN > string->__length || length < 0)  {
        iN = string->__length;
    }

    substr = stralloc(sizeof(mstring_t));
    if (substr == NULL) {
        return NULL;
    }

    substr->__flags = 0;
    substr->__length = iN - i0;
    substr->__data = stralloc(substr->__length * sizeof(mchar_t));
    if (substr->__data == NULL) {
        strfree(substr);
        return NULL;
    }

    for (i = 0; i0 < iN; i0++, i++) {
        substr->__data[i] = string->__data[i0];
    }
    return substr;
}
