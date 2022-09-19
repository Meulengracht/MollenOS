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

static int __append_u8(struct mstring_builder* builder, const char* string)
{
    size_t u8len = mstr_len_u8(string);
    return mstring_builder_append_u8(builder, string, u8len);
}

mstring_t* mstr_join(mstring_t** strings, int tokenCount, const char* sep)
{
    struct mstring_builder* builder;
    size_t                  sepLength = mstr_len_u8(sep);

    builder = mstring_builder_new(512);
    if (builder == NULL) {
        return NULL;
    }

    for (int i = 0; i < tokenCount; i++) {
        if (i != 0) {
            if (mstring_builder_append_u8(builder, sep, sepLength)) {
                mstring_builder_destroy(builder);
                return NULL;
            }
        }

        if (mstring_builder_append_mstring(builder, strings[i])) {
            mstring_builder_destroy(builder);
            return NULL;
        }
    }
    return mstring_builder_finish(builder);
}
