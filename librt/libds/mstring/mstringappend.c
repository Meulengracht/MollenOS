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

void
MStringAppend(
    _In_ MString_t* Destination,
    _In_ MString_t* String)
{
    uint8_t *StringPtr;
    size_t NewLength;

    assert(Destination != NULL);
    assert(String != NULL);

    NewLength = Destination->Length + String->Length;

    // Sanitize if destination has enough space for both buffers
    if (NewLength >= Destination->MaxLength) {
        MStringResize(Destination, NewLength);
    }

    StringPtr = (uint8_t*)Destination->Data;
    memcpy(StringPtr + Destination->Length, String->Data, String->Length);
    StringPtr[NewLength] = '\0';
    Destination->Length = NewLength;
}

void
MStringAppendCharacter(
    _In_ MString_t* String,
    _In_ mchar_t    Character)
{
    uint8_t*    StringPtr;
    size_t      NewLength;
    size_t      ExtraLength = 0;
    int         i           = 0;

    assert(String != NULL);

    NewLength = String->Length + Utf8ByteSizeOfCharacterInUtf8(Character);
    if (NewLength >= String->MaxLength) {
        MStringResize(String, NewLength);
    }
    StringPtr   = (uint8_t*)String->Data;
    i           = String->Length;

    Utf8ConvertCharacterToUtf8(Character, (void*)&StringPtr[i], &ExtraLength);
    StringPtr[i + ExtraLength] = '\0';
    String->Length += ExtraLength;
}

void
MStringAppendCharacters(
    _In_ MString_t*    String,
    _In_ const char*   Characters,
    _In_ MStringType_t DataType)
{
    assert(String != NULL);
    assert(Characters != NULL);

    MString_t* Proxy = MStringCreate(Characters, DataType);
    MStringAppend(String, Proxy);
    MStringDestroy(Proxy);
}
