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
 * - Returns a character back into the stream
 */

#include <errno.h>
#include <internal/_io.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

wint_t ungetwc(
    _In_ wint_t wc, 
    _In_ FILE *file)
{
    stdio_handle_t* handle;
    wchar_t         mwc = wc;

    if (wc == WEOF) {
        return WEOF;
    }
    
    if (!file) {
        _set_errno(EINVAL);
        return WEOF;
    }
    _lock_file(file);
    
    handle = stdio_handle_get(file->_fd);
    if ((handle->wxflag & WX_UTF) || !(handle->wxflag & WX_TEXT)) {
        unsigned char *pp = (unsigned char *)&mwc;
        int i;

        for (i = sizeof(wchar_t) - 1; i >= 0; i--) {
            if (pp[i] != ungetc(pp[i], file)) {
                _unlock_file(file);
                return WEOF;
            }
        }
    }
    else {
        char mbs[MB_LEN_MAX];
        int len;

        len = wctomb(mbs, mwc);
        if (len == -1) {
            _unlock_file(file);
            return WEOF;
        }

        for (len--; len >= 0; len--) {
            if (mbs[len] != ungetc(mbs[len], file)) {
                _unlock_file(file);
                return WEOF;
            }
        }
    }
    _unlock_file(file);
    return mwc;
}
