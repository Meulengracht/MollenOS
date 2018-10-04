/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * MollenOS MCore - String Format
 */

#include "mstringprivate.h"

/* MStringSubString
 * Build substring from the given mstring starting at Index with the Length. 
 * If the length is -1 it takes the rest of string */
MString_t*
MStringSubString(
    _In_ MString_t* String,
    _In_ int        Index,
    _In_ int        Length)
{
    MString_t*  SubString;
    char*       StringPtr;
    size_t      DataAllocLength;
    size_t      DataLength = 0;

    int cIndex = 0, i = 0;
    int StartIndex = -1;
    int LastIndex;

    if (String == NULL || String->Data == NULL || String->Length == 0) {
        return NULL;
    }
    
    // Santize the index/length given 
    // to make sure we don't hit bad lengths
    if (Index > (int)String->Length || ((((Index + Length) > (int)String->Length)) && Length != -1)) {
        return NULL;
    }
    StringPtr = (char*)String->Data;

    // Count how many bytes we actually need to copy
    // from the start-index, save start index
    while (i < String->Length) {
        LastIndex = i; // Store the byte-index
        if (Utf8GetNextCharacterInString(StringPtr, &i) == MSTRING_EOS) {
            break;
        }

        // Sanitize that we have entered
        // the index to record from, and make sure to record the start index 
        if (cIndex >= Index && (Length == -1 || (cIndex < (Index + Length)))) {
            if (StartIndex == -1) {
                StartIndex = LastIndex;
            }
            DataLength += (i - LastIndex);
        }
        cIndex++;
    }

    if (DataLength == 0) {
        return NULL;
    }

    // Create the actual substring copy
    SubString       = (MString_t*)dsalloc(sizeof(MString_t));
    DataAllocLength = DIVUP((DataLength + 1), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

    SubString->Data         = dsalloc(DataAllocLength);
    SubString->Length       = DataLength;
    SubString->MaxLength    = DataAllocLength;

    memset(SubString->Data, 0, DataAllocLength);
    memcpy(SubString->Data, ((uint8_t*)String->Data + StartIndex), DataLength);
    return SubString;
}
