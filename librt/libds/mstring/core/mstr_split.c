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

mstring_t* __build_from_to(mstring_t* original, size_t* index, mchar_t stopAt) {
    struct mstring_builder* builder;

    builder = mstring_builder_new(original->__length);
    if (builder == NULL) {
        return NULL;
    }

    for (size_t i = *index; i < original->__length; i++) {
        mchar_t val = original->__data[i];
        if (val == stopAt) {
            *index = (i + 1);
            break;
        }

        if (mstring_builder_append(builder, val)) {
            mstring_builder_destroy(builder);
            return NULL;
        }
    }
    return mstring_builder_finish(builder);
}

int mstr_split(mstring_t* string, mchar_t sep, mstring_t*** stringsOut)
{
    mstring_t** strings;
    int         stringsCount = 1;

    for (size_t i = 0; i < string->__length; i++) {
        mchar_t val = string->__data[i];
        if (val == sep) {
            stringsCount++;
        }
    }

    // If no storage is provided we assume the user just wanted
    // to know the number of tokens
    if (stringsOut == NULL) {
        return stringsCount;
    }

    strings = stralloc(stringsCount * sizeof(mstring_t*));
    if (strings == NULL) {
        return -1;
    }

    size_t state = 0;
    for (int i = 0; i < stringsCount; i++) {
        strings[i] = __build_from_to(string, &state, sep);
        if (strings[i] == NULL) {
            mstrv_delete(strings);
            return -1;
        }
    }

    *stringsOut = strings;
    return stringsCount;
}
