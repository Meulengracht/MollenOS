/**_tokens
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

uint32_t mstr_hash(mstring_t* string)
{
    uint32_t hash = 5381;
    size_t   i    = 0;

    if (string == NULL || string->__length == 0) {
        return 0;
    }

    while (i < string->__length) {
        hash = ((hash << 5) + hash) + string->__data[i++]; /* hash * 33 + c */
    }
    return hash;
}
