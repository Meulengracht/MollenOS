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

// Tokens are only counted as tokens if they non-zero
// and the initial seperator is not counted as a token.
// //path//path//.././/////
//    0     1    2  3

int mstr_path_tokens(mstring_t* path, mstring_t*** tokensOut)
{
    mstring_t** tokens;
    int         tokenCount = 0;

    // Count up tokens in path
    int skipSeperators = 1;
    for (size_t i = 0; i < path->__length; i++) {
        mchar_t val = path->__data[i];
        if (val != '/' && skipSeperators) {
            skipSeperators = 0;
            tokenCount++;
        } else if (val == '/') {
            skipSeperators = 1;
        }
    }

    // If no storage is provided we assume the user just wanted
    // to know the number of tokens
    if (tokensOut == NULL) {
        return tokenCount;
    }

    tokens = stralloc(tokenCount * sizeof(mstring_t*));
    if (tokens == NULL) {
        return -1;
    }

    for (int i = 0; i < tokenCount; i++) {
        tokens[i] = mstr_path_token_at(path, i);
        if (tokens[i] == NULL) {
            mstr_delete_array(tokens, i);
            return -1;
        }
    }

    *tokensOut = tokens;
    return tokenCount;
}

mstring_t* mstr_path_token_at(mstring_t* path, int index)
{
    struct mstring_builder* builder;
    int                     count = index;
    size_t                  i     = 0;

    builder = mstring_builder_new(128);
    if (builder == NULL) {
        return NULL;
    }

    int skipSeperators = 1;
    for (; i < path->__length; i++) {
        mchar_t val = path->__data[i];
        if (val != '/' && skipSeperators) {
            skipSeperators = 0;
            count--;
            if (!count) {
                break;
            }
        } else if (val == '/') {
            skipSeperators = 1;
        }
    }

    // If count is non-zero at this point, we asked for a token
    // that was out of range, so exit here
    if (count) {
        mstring_builder_destroy(builder);
        return NULL;
    }

    while (i < path->__length && path->__data[i] != '/') {
        if (mstring_builder_append(builder, path->__data[i++])) {
            mstring_builder_destroy(builder);
            return NULL;
        }
    }

    return mstring_builder_finish(builder);
}
