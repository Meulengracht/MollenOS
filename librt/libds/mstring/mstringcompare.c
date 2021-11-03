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
    char* StringPtr1;
    char* StringPtr2;
    int   i1 = 0;
    int   i2 = 0;

    if (!String1 || !String1->Data || String1->Length == 0 ||
        !String2 || !String2->Data || String2->Length == 0) {
        return MSTRING_NO_MATCH;
    }
    StringPtr1 = (char*)String1->Data;
    StringPtr2 = (char*)String2->Data;

    while ((i1 < String1->Length) && (i2 < String2->Length)) {
        mchar_t First  = Utf8GetNextCharacterInString(StringPtr1, &i1);
        mchar_t Second = Utf8GetNextCharacterInString(StringPtr2, &i2);
        if (First == MSTRING_EOS || Second == MSTRING_EOS) {
            return MSTRING_PARTIAL_MATCH;
        }

        // We only support case-insensitivity on ascii characters
        if (IgnoreCase) {
            if (First < 0x80 && isalpha(First)) {
                First = tolower((uint8_t)First);
            }
            if (Second < 0x80 && isalpha(Second)) {
                Second = tolower((uint8_t)Second);
            }
        }

        if (First != Second) {
            return MSTRING_NO_MATCH;
        }
    }

    if (StringPtr1[i1] != StringPtr2[i2]) {
        return MSTRING_NO_MATCH;
    }

    if (String1->Length != String2->Length) {
        return MSTRING_PARTIAL_MATCH;
    }
    return MSTRING_FULL_MATCH;
}
