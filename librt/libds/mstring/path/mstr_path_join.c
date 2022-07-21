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
#include <stdarg.h>

static int __append_u8(struct mstring_builder* builder, const char* string)
{
    size_t u8len = mstr_len_u8(string);
    return mstring_builder_append_u8(builder, string, u8len);
}

mstring_t* mstr_path_join(mstring_t* base, ...)
{
    struct mstring_builder* builder;
    va_list                 args;

    builder = mstring_builder_new(512);
    if (builder == NULL) {
        return NULL;
    }

    if (mstring_builder_append_mstring(builder, base)) {
        mstring_builder_destroy(builder);
        return NULL;
    }

    va_start(args, base);
    while (1) {
        mstring_t* token = va_arg(args, mstring_t*);
        if (token == NULL) {
            break;
        }

        if (mstring_builder_append(builder, '/') ||
            mstring_builder_append_mstring(builder, token)) {
            mstring_builder_destroy(builder);
            return NULL;
        }
    }
    va_end(args);
    return mstring_builder_finish(builder);
}

mstring_t* mstr_path_join_u8(mstring_t* base, ...)
{
    struct mstring_builder* builder;
    va_list                 args;

    builder = mstring_builder_new(512);
    if (builder == NULL) {
        return NULL;
    }

    if (mstring_builder_append_mstring(builder, base)) {
        mstring_builder_destroy(builder);
        return NULL;
    }

    va_start(args, base);
    while (1) {
        const char* token = va_arg(args, const char*);
        if (token == NULL) {
            break;
        }

        if (mstring_builder_append(builder, '/') ||
            __append_u8(builder, token)) {
            mstring_builder_destroy(builder);
            return NULL;
        }
    }
    va_end(args);
    return mstring_builder_finish(builder);
}
