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

#include <ds/mstring.h>
#include <stdlib.h>

mchar_t mstr_at(mstring_t* string, int index)
{
    // We support negative indexing, so abs it, so we can make sure
    // we don't go out of index for either way
    int absIndex = abs(index);
    if (string == NULL || absIndex >= string->__length) {
        return 0;
    }

    if (index < 0) {
        return *(mchar_t*)(string->__data + ((int)string->__length + index));
    }
    return string->__data[index];
}
