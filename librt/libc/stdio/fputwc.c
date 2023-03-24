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
 */

#include "errno.h"
#include "internal/_file.h"
#include "internal/_io.h"
#include "stdio.h"
#include "stdlib.h"
#include "wchar.h"
 
wint_t fputwc(
    _In_ wchar_t c,
    _In_ FILE*   stream)
{
    stdio_handle_t* handle;
    if (!stream) {
        _set_errno(EINVAL);
        return WEOF;
    }
    
    handle = stdio_handle_get(stream->IOD);
    if (!handle) {
        _set_errno(EBADFD);
        return WEOF;
    }

    /* If this is a real file stream (and not some temporary one for
       sprintf-like functions), check whether it is opened in text mode.
       In this case, we have to perform an implicit conversion to ANSI. */
    if (!__FILE_IsStrange(stream) && handle->XTFlags & __IO_TEXTMODE) {
        /* Convert to multibyte in text mode */
        char mbc[MB_LEN_MAX];
        int mb_return;

        mb_return = wctomb(mbc, c);
        if(mb_return == -1) {
            return WEOF;
        }
        if (fwrite(mbc, mb_return, 1, stream) != 1) {
            return WEOF;
        }
    } else {
        if (fwrite(&c, sizeof(c), 1, stream) != 1)
            return WEOF;
    }
    return c;
}
