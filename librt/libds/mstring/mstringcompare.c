/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Generic String Library
 *    - Managed string library for manipulating of strings in a managed format and to support
 *      conversions from different formats to UTF-8
 */

#include "mstringprivate.h"

/* MStringCompare
 * Compare two strings with either case-insensitivity or not. 
 * Returns 
 *   - MSTRING_FULL_MATCH           If they are equal
 *   - MSTRING_PARTIAL_MATCH        If they contain same text but one of the strings are longer
 *   - MSTRING_NO_MATCH             If they don't match */
int
MStringCompare(
    _In_ MString_t* String1,
    _In_ MString_t* String2,
    _In_ int        IgnoreCase)
{
    char* data1;
    char* data2;
    int   i1 = 0;
    int   i2 = 0;

    // verify data is atleast available
    if (!String1 || !String1->Data || !String2 || !String2->Data) {
        return MSTRING_NO_MATCH;
    }
    data1 = (char*)String1->Data;
    data2 = (char*)String2->Data;

    while ((i1 < String1->Length) && (i2 < String2->Length)) {
        mchar_t first  = Utf8GetNextCharacterInString(data1, &i1);
        mchar_t second = Utf8GetNextCharacterInString(data2, &i2);
        if (first == MSTRING_EOS || second == MSTRING_EOS) {
            return MSTRING_PARTIAL_MATCH;
        }

        // We only support case-insensitivity on ascii characters
        if (IgnoreCase) {
            if (first < 0x80 && isalpha(first)) {
                first = tolower((uint8_t)first);
            }
            if (second < 0x80 && isalpha(second)) {
                second = tolower((uint8_t)second);
            }
        }

        if (first != second) {
            return MSTRING_NO_MATCH;
        }
    }

    if (String1->Length != String2->Length) {
        return MSTRING_PARTIAL_MATCH;
    }
    return MSTRING_FULL_MATCH;
}
