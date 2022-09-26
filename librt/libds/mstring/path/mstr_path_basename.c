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

mstring_t* mstr_path_basename(mstring_t* path)
{
    mstring_t*  basename;
    mstring_t** tokens;
    int         tokenCount;

    tokenCount = mstr_path_tokens(path, &tokens);
    if (tokenCount < 0) {
        return NULL;
    }

    // If no tokens are present, we return an empty string unless
    // the path equals '/'
    if (tokenCount == 0) {
        mstrv_delete(tokens);

        if (path->__data[0] == U'/') {
            return mstr_new_u8("/");
        }

        // otherwise we return an empty string
        return mstr_new_u8("");
    }

    // Otherwise we simply return the last token
    basename = mstr_clone(tokens[tokenCount - 1]);
    mstrv_delete(tokens);
    return basename;
}
