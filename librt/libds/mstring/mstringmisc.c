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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic String Library
 *    - Managed string library for manipulating of strings in a managed format and to support
 *      conversions from different formats to UTF-8
 */

#include "mstringprivate.h"

static int __IsMatch(
        const char* string1,
        const char* string2,
        size_t      count)
{
    size_t i = 0;
    while (i < count && string1[i] == string2[i]) {
        i++;
    }
    return i != count;
}

int
MStringFindC(
    _In_ MString_t*  text,
    _In_ const char* lookFor)
{
    const char* data;
    int         i  = 0;
    int         ci = 0;
    size_t      lookForLength = strlen(lookFor);

    if (!text || !text->Length || !lookForLength) {
        goto _exit;
    }

    data = (const char*)text->Data;
    while (i < text->Length) {
        if (!__IsMatch(&data[i], lookFor, lookForLength)) {
            return ci;
        }
        Utf8GetNextCharacterInString(data, &i);
        ci++;
    }

_exit:
    errno = ENOENT;
    return MSTRING_NOT_FOUND;
}

int MStringReplaceC(
        _In_ MString_t*  text,
        _In_ const char* lookFor,
        _In_ const char* replaceWith)
{
    const char* source;
    char*       dest;
    int         sourceIndex;
    int         destIndex;
    size_t      lookForLength     = strlen(lookFor);
    size_t      replaceWithLength = strlen(replaceWith);
    int         matchFound        = 0;

    if (!text || !text->Data || !lookForLength || !replaceWithLength) {
        errno = EINVAL;
        return -1;
    }

    source = (const char*)text->Data;
    dest   = dsalloc(1024);
    if (!dest) {
        errno = ENOMEM;
        return -1;
    }
    memset(dest, 0, 1024);

    // now iterate through source string and look for match
    sourceIndex = 0;
    destIndex   = 0;
    while (sourceIndex < text->Length) {
        if (!__IsMatch(&source[sourceIndex], lookFor, lookForLength)) {
            memcpy(&dest[destIndex], replaceWith, replaceWithLength);
            matchFound = 1;

            sourceIndex += (int)lookForLength;
            destIndex   += (int)replaceWithLength;
        }
        else {
            dest[destIndex++] = source[sourceIndex++];
        }
    }

    if (matchFound) {
        MStringReset(text, (const char*)dest, StrUTF8);
    }
    dsfree(dest);
    return (matchFound == 0) ? MSTRING_NO_MATCH : MSTRING_FULL_MATCH;
}

void
MStringGetAscii(
    _In_ MString_t  *String,
    _Out_ char      *Buffer,
    _In_ size_t      BufferLength)
{
    // Variables
    char *DestinationPointer    = NULL;
    char *SourcePointer         = NULL;
    int iSource                 = 0;
    int iDest                   = 0;
    int Count                   = 0;
    uint32_t Value = 0;

    // Sanitize input
    if (String == NULL || String->Length == 0
        || Buffer == NULL || BufferLength == 0) {
        return;
    }

    // Instantiate pointers
    DestinationPointer  = Buffer;
    SourcePointer       = (char*)String->Data;
    while (SourcePointer[iSource]) {
        unsigned char uChar = (unsigned char)SourcePointer[iSource];

        // Normal ascii character case
        if (uChar < 0x80) {
            DestinationPointer[iDest]   = SourcePointer[iSource];
            iDest++;
        }
        else if ((uChar & 0xC0) == 0xC0) { // UTF-8 Character lead-byte
            if (Count > 0) { // Sanitize
                break;
            }

            // How many bytes are taken by next character?
            if ((uChar & 0xF8) == 0xF0) {
                Value = uChar & 0x07;
                Count = 3;
            }
            else if ((uChar & 0xF0) == 0xE0) {
                Value = uChar & 0x0F;
                Count = 2;
            }
            else if ((uChar & 0xE0) == 0xC0) {
                Value = uChar & 0x1F;
                Count = 1;
            }
            else { // Invalid
                break;
            }
        }
        else {
            Value <<= 6;
            Value |= uChar & 0x3F;
            if (--Count <= 0) {
                if (Value <= 0xFF) {
                    DestinationPointer[iDest] = SourcePointer[iSource];
                    iDest++;
                }
            }
        }

        // Move on
        iSource++;
    }
}
