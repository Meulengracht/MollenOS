/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS String Library
 *  - Contains implementation of a generic string library that can
 *    hold data in an UTF8 maner. It can convert all typical string formats
 *    to UTF-8.
 */

/* Includes
 * - System */
#include "mstringprivate.h"

/* MStringFindCString
 * Find first occurence of the given UTF8 string
 * in the given string. This does not accept UTF16 or UTF32.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int
MStringFindCString(
    _In_ MString_t  *String,
    _In_ const char *Chars)
{
    // Variables
    char *SourcePointer = NULL;
    int Result          = MSTRING_NO_MATCH;
    int iSource         = 0;
    int iDest           = 0;

    // Sanitize input
    if (String == NULL || String->Length == 0) {
        goto Out;
    }

    // Initiate pointer
    SourcePointer           = (char*)String->Data;
    
    // Iterate all characters and try to find a match
    mchar_t SourceCharacter = 0;
    mchar_t DestCharacter   = 0;
    while (SourcePointer[iSource]) {
        iDest               = 0;
        SourceCharacter     = Utf8GetNextCharacterInString(SourcePointer, &iSource);
        DestCharacter       = Utf8GetNextCharacterInString(Chars, &iDest);

        // Test for equal, if they are continue match
        if (SourceCharacter == DestCharacter) {
            int iTemp       = iSource;
            while (SourcePointer[iTemp] && Chars[iDest] 
                && SourceCharacter == DestCharacter) {
                SourceCharacter = Utf8GetNextCharacterInString(SourcePointer, &iTemp);
                DestCharacter   = Utf8GetNextCharacterInString(Chars, &iDest);
            }

            // Make sure final character is still equal after loop
            if (SourceCharacter == DestCharacter) {
                if (!Chars[iDest]) { // Must be end of string, otherwise there are chars left
                    break;
                }
            }
        }
        Result++;
    }

    // Cleanup and return
Out:
    return Result;
}

/* MStringReplace
 * Replace string occurences, this function replaces occurence of <SearchFor> string 
 * with <ReplaceWith> string. The strings must be of format of UTF8. returns MSTRING_NO_MATCH
 * on any errors. */
int
MStringReplace(
    _In_ MString_t  *String,
    _In_ const char *SearchFor,
    _In_ const char *ReplaceWith)
{
    // Variables
    char *TemporaryBuffer   = NULL;
    char *TemporaryPointer  = NULL;
    char *SourcePointer     = NULL;
    int Result              = MSTRING_NO_MATCH;
    int iSource             = 0;
    int iDest               = 0;
    int iLast               = 0;
    size_t ReplaceLength    = 0;

    // Sanitize input
    if (String == NULL || String->Length == 0
        || SearchFor == NULL || ReplaceWith == NULL) {
        goto Out;
    }

    // Initialize pointers
    SourcePointer       = (char*)String->Data;
    ReplaceLength       = strlen(ReplaceWith);
    TemporaryBuffer     = (char*)dsalloc(1024);
    TemporaryPointer    = TemporaryBuffer;
    memset(TemporaryBuffer, 0, 1024);

     // Iterate all characters and try to find a match
     // between string and SearchFor
    mchar_t SourceCharacter = 0;
    mchar_t DestCharacter   = 0;
    while (SourcePointer[iSource]) {
        iLast               = iSource;
        iDest               = 0;
        SourceCharacter     = Utf8GetNextCharacterInString(SourcePointer, &iSource);
        DestCharacter       = Utf8GetNextCharacterInString(SearchFor, &iDest);

        // Test for equal, if they are continue match
        if (SourceCharacter == DestCharacter) {
            int iTemp       = iSource;
            while (SourcePointer[iTemp] && SearchFor[iDest] 
                && SourceCharacter == DestCharacter) {
                SourceCharacter = Utf8GetNextCharacterInString(SourcePointer, &iTemp);
                DestCharacter   = Utf8GetNextCharacterInString(SearchFor, &iDest);
            }

            // Make sure final character is still equal after loop
            if (SourceCharacter == DestCharacter) {
                if (!SearchFor[iDest]) { // Must be end of string, otherwise there are chars left
                    Result          = MSTRING_FULL_MATCH;
                    memcpy(TemporaryBuffer, ReplaceWith, ReplaceLength);
                    TemporaryBuffer += ReplaceLength;
                    continue;
                }
            }
        }

        // Continue building a resulting string on the fly
        memcpy(TemporaryBuffer, &SourcePointer[iLast], (iSource - iLast));
        TemporaryBuffer += (iSource - iLast);
    }

    // Did we find any matches? Otherwise early quit
    if (Result == MSTRING_NO_MATCH) {
        goto Out;
    }

    // Reconstruct the string into source
    ReplaceLength = strlen((const char*)TemporaryPointer);
    if (ReplaceLength > String->MaxLength) {
        MStringResize(String, ReplaceLength);
    }

    // Copy the data
    memcpy(String->Data, TemporaryPointer, ReplaceLength);
    String->Length = ReplaceLength;

    // Cleanup and return
Out:
    if (TemporaryPointer != NULL) {
        dsfree(TemporaryPointer);
    }
    return Result;
}

/* MStringGetAscii
 * Converts the given MString into Ascii string and stores it
 * in the given buffer. Unicode characters are truncated. */
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
