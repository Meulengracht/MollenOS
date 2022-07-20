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

mstring_t* mstr_clone(mstring_t* source)
{
    mstring_t* string;

    string = stralloc(sizeof(mstring_t));
    if (string == NULL) {
        return NULL;
    }

    string->__flags = 0;
    string->__length = source->__length;
    if (string->__length != 0) {
        string->__data = stralloc(string->__length * sizeof(mchar_t));
        if (string->__data == NULL) {
            strfree(string);
            return NULL;
        }
        memcpy(string->__data, source->__data, string->__length * sizeof(mchar_t));
    } else {
        string->__data = NULL;
    }
    return string;
}
