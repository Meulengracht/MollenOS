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
    _In_ MString_t* destination,
    _In_ MString_t* source)
{
    uint8_t* data;
    size_t   newLength;

    if (!destination || !source) {
        return;
    }

    // ensure that we have enough space in destination
    newLength = destination->Length + source->Length;
    MStringResize(destination, newLength);

    data = (uint8_t*)destination->Data;
    memcpy(data + destination->Length, source->Data, source->Length);
    data[newLength] = '\0';
    destination->Length = newLength;
}

void
MStringAppendCharacter(
    _In_ MString_t* string,
    _In_ mchar_t    character)
{
    uint8_t* data;
    size_t   newLength;
    size_t   extraLength = 0;
    int      i;

    if (!string) {
        return;
    }

    newLength = string->Length + Utf8ByteSizeOfCharacterInUtf8(character);
    MStringResize(string, newLength);

    data = (uint8_t*)string->Data;
    i    = string->Length;

    Utf8ConvertCharacterToUtf8(character, (void*)&data[i], &extraLength);
    data[i + extraLength] = '\0';

    string->Length += extraLength;
}

void
MStringAppendCharacters(
    _In_ MString_t*    string,
    _In_ const char*   characters,
    _In_ MStringType_t dataType)
{
    MString_t* proxy;

    if (!string || !characters) {
        return;
    }

    proxy = MStringCreate(characters, dataType);
    MStringAppend(string, proxy);
    MStringDestroy(proxy);
}
