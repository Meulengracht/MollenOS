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
#include <string.h>

static int __get_rindex(mstring_t* string, mchar_t val) {
    for (int i = (int)string->__length; i >= 0; i--) {
        if (string->__data[i] == val) {
            return i;
        }
    }
    return -1;
}

mstring_t* mstr_path_change_extension_u8(mstring_t* path, const char* ext)
{
    mstring_t*  resultPath;
    mstring_t** tokens;
    int         tokenCount;
    size_t      extLength = strlen(ext);

    tokenCount = mstr_path_tokens(path, &tokens);
    if (tokenCount < 0) {
        return NULL;
    }

    if (tokenCount > 0) {
        struct mstring_builder* builder = mstring_builder_new(128);
        int                     dotAt   = __get_rindex(tokens[tokenCount - 1], U'.');

        // build string up until the dot character, and if the dot character wasn't found
        // then we do not do any replacements
        if (dotAt >= 0) {
            for (int i = 0; i < dotAt; i++) {
                if (mstring_builder_append(builder, tokens[tokenCount - 1]->__data[i])) {
                    mstring_builder_destroy(builder);
                    mstrv_delete(tokens);
                    return NULL;
                }
            }

            // now append the new extension instead
            if (mstring_builder_append_u8(builder, ext, extLength)) {
                mstring_builder_destroy(builder);
                mstrv_delete(tokens);
                return NULL;
            }

            // free the old index, and replace it with what we just built.
            mstr_delete(tokens[tokenCount - 1]);
            tokens[tokenCount - 1] = mstring_builder_finish(builder);
            if (tokens[tokenCount - 1] == NULL) {
                mstrv_delete(tokens); // this is ok
                return NULL;
            }
        }
    }

    resultPath = mstr_path_tokens_join(tokens, tokenCount);
    mstrv_delete(tokens);
    return resultPath;
}
