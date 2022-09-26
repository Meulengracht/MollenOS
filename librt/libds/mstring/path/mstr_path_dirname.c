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

static int __append_mstring(struct mstring_builder* builder, mstring_t* string)
{
    for (size_t i = 0; i < string->__length; i++) {
        if (mstring_builder_append(builder, string->__data[i])) {
            return -1;
        }
    }
    return 0;
}

mstring_t* mstr_path_dirname(mstring_t* path)
{
    struct mstring_builder* builder;
    mstring_t**             tokens;
    int                     tokenCount;

    builder = mstring_builder_new(path->__length);
    if (builder == NULL) {
        return NULL;
    }

    tokenCount = mstr_path_tokens(path, &tokens);
    if (tokenCount < 0) {
        mstring_builder_destroy(builder);
        return NULL;
    }

    // Add initial seperator?
    if (path->__data[0] == U'/') {
        if (mstring_builder_append(builder, path->__data[0])) {
            mstring_builder_destroy(builder);
            return NULL;
        }

        if (tokenCount <= 1) {
            goto finish;
        }
    } else if (tokenCount <= 1) {
        if (mstring_builder_append(builder, U'.')) {
            mstring_builder_destroy(builder);
            return NULL;
        }
        goto finish;
    }

    for (int i = 0; i < (tokenCount - 1); i++) {
        if (__append_mstring(builder, tokens[i])) {
            mstring_builder_destroy(builder);
            return NULL;
        }

        // Only append '/' if we are not at second-last token. The
        // last token will be the basename, which we exclude, and
        // only on tokens before that we insert a seperator
        if (i != (tokenCount - 2)) {
            if (mstring_builder_append(builder, U'/')) {
                mstring_builder_destroy(builder);
                return NULL;
            }
        }
    }

finish:
    mstrv_delete(tokens);
    return mstring_builder_finish(builder);
}
