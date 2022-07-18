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
#include "mstr_conv.h"

//                         X (start = 0, end = 16, i = 8)
//                                         X (start = 8, end = 16, i = 12)
//                               X (start = 8, end = 12, i = 10)
//                                    X (start = 10, end = 12, i = 11)
//                                    X (start = 11, end = 12, i = 11)
// 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, [11], 12, 13, 14, 15

#define GO_NEXT(start, end) ((start) + (((end) - (start)) >> 2))

mchar_t __find_binary_search(mchar_t val, const case_folding_t* table, size_t length)
{
    size_t start = 0, end = length;
    size_t i = GO_NEXT(start, end);

    do {
        if (table[i].code == val) {
            return table[i].folded_code;
        } else if (val > table[i].code) {
            start = i;
        } else {
            end = i;
        }

        // Are we at last possible value?
        if (start == i) {
            break;
        }
        i = GO_NEXT(start, end);
    } while (1);
    return val;
}

mchar_t mstr_clower(mchar_t val)
{
    return __find_binary_search(val, g_lowerCaseTable, g_lowerCaseTableSize);
}

mchar_t mstr_cupper(mchar_t val)
{
    return __find_binary_search(val, g_upperCaseTable, g_upperCaseTableSize);
}
