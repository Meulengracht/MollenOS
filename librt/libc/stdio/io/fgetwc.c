/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Library
 * - Returns the next character from the given stream.
 */

#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "../stdlib/mb/mbctype.h"
#include "../libc_io.h"

wint_t fgetwc(FILE *stream)
{
	stdio_object_t* object;
    wint_t          Result;
    int             ch;
    
    if (!stream) {
        _set_errno(EINVAL);
        return WEOF;
    }

    _lock_file(stream);
    object = stdio_object_get(stream->_fd);
    if (!object) {
        _unlock_file(stream);
        _set_errno(EBADFD);
        return WEOF;
    }

    if ((object->wxflag & WX_UTF) || !(object->wxflag & WX_TEXT)) {
        char *p;
        for (p = (char *)&Result; (wint_t *)p < &Result + 1; p++) {
            ch = fgetc(stream);
            if (ch == EOF) {
                Result = WEOF;
                break;
            }
            *p = (char)ch;
        }
    }
    else
    {
        char mbs[MB_LEN_MAX];
        int len = 0;

        ch = fgetc(stream);
        if (ch != EOF) {
            mbs[0] = (char)ch;
            if (_issjis1((unsigned char)mbs[0])) {
                ch = fgetc(stream);
                if (ch != EOF)
                {
                    mbs[1] = (char)ch;
                    len = 2;
                }
            }
            else {
                len = 1;
            }
        }

        if (!len || mbtowc((wchar_t*)&Result, mbs, len) == -1)
            Result = WEOF;
    }
    _unlock_file(stream);
    return Result;
}
