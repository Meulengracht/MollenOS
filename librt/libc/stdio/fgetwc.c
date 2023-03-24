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
#include "internal/_io.h"
#include "internal/_file.h"
#include "stdio.h"
#include "stdlib.h"
#include "wchar.h"
#include "../stdlib/mb/mbctype.h"

wint_t fgetwc(FILE *stream)
{
	stdio_handle_t* handle;
    wint_t          result;
    int             ch;
    
    if (!stream) {
        errno = (EINVAL);
        return WEOF;
    }

    handle = stdio_handle_get(stream->IOD);
    if (!handle) {
        _set_errno(EBADFD);
        return WEOF;
    }

    if ((handle->XTFlags & __IO_UTF) || !(handle->XTFlags & __IO_TEXTMODE)) {
        for (char* p = (char *)&result; (wint_t *)p < &result + 1; p++) {
            ch = fgetc(stream);
            if (ch == EOF) {
                result = WEOF;
                break;
            }
            *p = (char)ch;
        }
    } else {
        char mbs[MB_LEN_MAX];
        int  len = 0;

        ch = fgetc(stream);
        if (ch != EOF) {
            mbs[0] = (char)ch;
            if (_issjis1((unsigned char)mbs[0])) {
                ch = fgetc(stream);
                if (ch != EOF) {
                    mbs[1] = (char)ch;
                    len = 2;
                }
            } else {
                len = 1;
            }
        }

        if (!len || mbtowc((wchar_t*)&result, mbs, len) == -1) {
            result = WEOF;
        }
    }
    return result;
}
