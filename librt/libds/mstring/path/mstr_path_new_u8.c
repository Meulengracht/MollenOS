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

mstring_t* mstr_path_new_u8(const char* u8)
{
    struct mstring_builder* builder;
    int                     u8i = 0;

    builder = mstring_builder_new(512);
    if (builder == NULL) {
        return NULL;
    }

    mchar_t prev = 0;
    while (u8[u8i]) {
        mchar_t val = mstr_next(u8, &u8i);

        // Always correct backslashes to slashes
        if (val == '\\') { val = '/'; }

        // If the last character was a slash, then skip this one, we do
        // not want double-slashes in paths
        if (val == U'/' && prev == U'/') { continue; }

        if (mstring_builder_append(builder, val)) {
            mstring_builder_destroy(builder);
            return NULL;
        }

        prev = val;
    }

    return mstring_builder_finish(builder);
}
