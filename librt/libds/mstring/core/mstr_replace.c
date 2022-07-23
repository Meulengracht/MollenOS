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

mstring_t* mstr_replace_u8(mstring_t* string, const char* find, const char* with)
{
    struct mstring_builder* builder;
    int     u8i = 0;
    size_t  i   = 0;
    size_t  findLength;
    size_t  withLength;
    mchar_t needle;

    if (string == NULL || find == NULL || with == NULL) {
        return NULL;
    }

    builder = mstring_builder_new(string->__length + 1);
    if (builder == NULL) {
        return NULL;
    }

    withLength = mstr_len_u8(with);
    findLength = mstr_len_u8(find);

    // to speed up things, we look for the initial character before
    // making any compare calls.
    needle = mstr_next(find, &u8i);
    while (i < string->__length) {
        if (needle == string->__data[i]) {
            // compare the entire u8 sequence
            if (!mstr_cmp_u8_index(string, find, i, findLength)) {
                if (mstring_builder_append_u8(builder, with, withLength)) {
                    mstring_builder_destroy(builder);
                    return NULL;
                }
                i += findLength;
                continue;
            }
        }

        if (mstring_builder_append(builder, string->__data[i])) {
            mstring_builder_destroy(builder);
            return NULL;
        }
        i++;
    }
    return mstring_builder_finish(builder);
}
