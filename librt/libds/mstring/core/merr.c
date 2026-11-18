/**
 * Copyright 2024, Philip Meulengracht
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

#include <ds/merr.h>
#include <ds/mstring.h>

merr_t merr_fmt(int code, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    mstring_t* message = mstr_fmt_v(fmt, args);
    va_end(args);

    return (merr_t){
        .code = code,
        .message = message
    };
}

int merr_code(merr_t* err) {
    return err->code;
}

void merr_free(merr_t* err) {
    if (err != NULL && err->message != NULL) {
        mstr_delete(err->message);
        err->message = NULL;
        err->code = 0;
    }
}
