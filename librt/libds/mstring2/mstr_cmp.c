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

#include <ds/mstring2.h>
#include "private.h"

int mstr_cmp(mstring_t* lh, mstring_t* rh)
{
    size_t i;

    if (lh == NULL || rh == NULL) {
        return -1;
    }

    if (lh->__length != rh->__length) {
        return -1;
    }

    for (i = 0; i < lh->__length; i++) {
        if (lh->__data[i] != rh->__data[i]) {
            return -1;
        }
    }
    return 0;
}

int mstr_icmp(mstring_t* lh, mstring_t* rh)
{
    size_t i;

    if (lh == NULL || rh == NULL) {
        return -1;
    }

    if (lh->__length != rh->__length) {
        return -1;
    }

    for (i = 0; i < lh->__length; i++) {
        mchar_t cl = mstr_cupper(lh->__data[i]);
        mchar_t cr = mstr_cupper(rh->__data[i]);
        if (cl != cr) {
            return -1;
        }
    }
    return 0;
}

int mstr_cmp_u8_index(mstring_t* string, const char* u8, size_t startIndex)
{
    size_t i   = startIndex;
    int    u8i = 0;

    if (string == NULL || u8 == NULL) {
        return -1;
    }

    for (; i < string->__length; i++) {
        mchar_t val = mstr_next(&u8[u8i], &u8i);
        if (val != string->__data[i]) {
            return -1;
        }
    }

    // OK strings seem to match, make sure we are at end of
    // the UTF-8 string as well
    if (u8[u8i]) {
        return -1;
    }
    return 0;
}

int mstr_cmp_u8(mstring_t* string, const char* u8)
{
    return mstr_cmp_u8_index(string, u8, 0);
}
