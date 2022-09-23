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

void mstr_delete(mstring_t* string)
{
    if (string == NULL) {
        return;
    }

    // consts have no actual memory allocated on heap
    if (string->__flags & __MSTRING_FLAG_CONST) {
        return;
    }
    strfree(string->__data);
    strfree(string);
}

void mstrv_delete(mstring_t** strings)
{
    if (strings == NULL) {
        return;
    }

    for (int i = 0; strings[i] != NULL; i++) {
        mstr_delete(strings[i]);
    }
    strfree(strings);
}
