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
    _In_ MString_t* String,
    _In_ int        Index,
    _In_ int        Length)
{
    MString_t*  SubString;
    char*       StringPtr;
    int         CurrentIndex    = 0;
    int         i               = 0;

    assert(String != NULL);

    // Create the string target
    SubString = MStringCreate(NULL, StrUTF8);
    
    // Santize the index/length given to make sure we don't hit bad lengths
    if (Index > (int)String->Length || ((((Index + Length) > (int)String->Length)) && Length != -1)) {
        return SubString;
    }
    StringPtr = (char*)String->Data;

    // Count how many bytes we actually need to copy
    // from the start-index, save start index
    while (i < String->Length) {
        mchar_t Character = Utf8GetNextCharacterInString(StringPtr, &i);
        if (Character == MSTRING_EOS) {
            break;
        }

        // Sanitize that we have entered
        // the index to record from, and make sure to record the start index 
        if (CurrentIndex >= Index && (Length == -1 || (CurrentIndex < (Index + Length)))) {
            MStringAppendCharacter(SubString, Character);
        }
        CurrentIndex++;
    }
    return SubString;
}
