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
        int                     dotAt   = -1;
        // locate the '.' from reverse in the last token, then we do a replacement
        for (int i = (int)tokens[tokenCount - 1]->__length; i >= 0; i--) {
            if (tokens[tokenCount - 1]->__data[i] == U'.') {
                dotAt = i;
                break;
            }
        }
        for (int i = 0; i < dotAt; i++) {
            mstring_builder_append(builder, tokens[tokenCount - 1]->__data[i]);
        }
        mstring_builder_append_u8(builder, ext, extLength);
        mstr_delete(tokens[tokenCount - 1]);
        tokens[tokenCount - 1] = mstring_builder_finish(builder);
    }

    resultPath = mstr_path_tokens_join(tokens, tokenCount);
    mstrv_delete(tokens);
    return resultPath;
}
