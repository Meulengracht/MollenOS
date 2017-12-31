/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - C Standard Library
 * - Returns the next character from the given stream.
 */

#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include "../stdlib/mb/mbctype.h"
#include "local.h"

wint_t fgetwc(
    _In_ FILE *file)
{
    wint_t ret;
    int ch;

    _lock_file(file);
    if ((get_ioinfo(file->_fd)->exflag & (EF_UTF8 | EF_UTF16)) 
        || !(get_ioinfo(file->_fd)->wxflag & WX_TEXT)) {
        
        char *p;
        for (p = (char *)&ret; (wint_t *)p < &ret + 1; p++) {
            ch = fgetc(file);
            if (ch == EOF) {
                ret = WEOF;
                break;
            }
            *p = (char)ch;
        }
    }
    else
    {
        char mbs[MB_LEN_MAX];
        int len = 0;

        ch = fgetc(file);
        if (ch != EOF) {
            mbs[0] = (char)ch;
            if (_issjis1((unsigned char)mbs[0])) {
                ch = fgetc(file);
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

        if (!len || mbtowc((wchar_t*)&ret, mbs, len) == -1)
            ret = WEOF;
    }
    _unlock_file(file);
    return ret;
}
