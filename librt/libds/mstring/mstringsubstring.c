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
#include <assert.h>

MString_t*
MStringSubString(
    _In_ MString_t* string,
    _In_ int        index,
    _In_ int        length)
{
    MString_t* subString = MStringCreate(NULL, StrUTF8);
    char*      stringPtr;
    int        currentIndex = 0;
    int        i            = 0;
    int        cappedLength = length;

    // Do not accept 0 length
    // Make sure index is within range and allowing for ATLEAST 1 character
    if (!string || !length || index >= (int)string->Length) {
        return subString;
    }

    // Make sure index + length does not exceed the capacity. Lets cap it in that case
    if (length < 0 || ((index + length) > (int)string->Length)) {
        cappedLength = (int)string->Length - index;
    }

    // Count how many bytes we actually need to copy
    // from the start-index, save start index
    stringPtr = (char*)string->Data;

    while (i < string->Length) {
        mchar_t next = Utf8GetNextCharacterInString(stringPtr, &i);
        if (next == MSTRING_EOS) {
            break;
        }

        // Sanitize that we have entered
        // the index to record from, and make sure to record the start index 
        if (currentIndex >= index && (currentIndex < (index + cappedLength))) {
            MStringAppendCharacter(subString, next);
        }
        currentIndex++;
    }
    return subString;
}
