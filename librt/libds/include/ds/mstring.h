/**
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

#ifndef __MSTRING_INTERFACE_H__
#define __MSTRING_INTERFACE_H__

#include <ds/dsdefs.h>

typedef uint32_t mchar_t;
#define MSTRING_NOT_FOUND		((int)-1)     // Returned if an occurence is not found
#define MSTRING_EOS				0xFFFD      // Marker for identifying end of string
#define MSTRING_NO_MATCH		0           // No match was found
#define MSTRING_FULL_MATCH		1           // A full match was found
#define MSTRING_PARTIAL_MATCH	2           // Partial match, means it contains the match

typedef enum MStringType {
	StrASCII,
	StrUTF8,
	StrUTF16,
	StrUTF32,
	Latin1
} MStringType_t;
struct MString;
typedef struct MString MString_t;

_CODE_BEGIN
DSDECL(MString_t*, MStringCreate(const char* Data, MStringType_t DataType));
DSDECL(MString_t*, MStringClone(MString_t* String));
DSDECL(void,       MStringZero(MString_t* String));
DSDECL(void,       MStringReset(MString_t* String, const char* NewString, MStringType_t DataType));
DSDECL(void,       MStringCopy(MString_t* Destination, MString_t* Source, int DestinationIndex, int SourceIndex, int Length));
DSDECL(void,       MStringDestroy(MString_t* string));

// Append Character to a given string the character is assumed to be either ASCII, UTF16 or UTF32
DSDECL(void, MStringPrepend(MString_t* destination, MString_t* source));
DSDECL(void, MStringAppend(MString_t* destination, MString_t* source));
DSDECL(void, MStringAppendCharacter(MString_t* string, mchar_t character));
DSDECL(void, MStringAppendCharacters(MString_t* string, const char* characters, MStringType_t dataType));
DSDECL(void, MStringAppendInt32(MString_t* String, int32_t Value));
DSDECL(void, MStringAppendUInt32(MString_t* String, uint32_t Value));
DSDECL(void, MStringAppendHex32(MString_t* String, uint32_t Value));
DSDECL(void, MStringAppendInt64(MString_t* String, int64_t Value));
DSDECL(void, MStringAppendUInt64(MString_t* String, uint64_t Value));
DSDECL(void, MStringAppendHex64(MString_t* String, uint64_t Value));

/*
 * Retrieves the index of the first occurence of the given character. Optionally a start-index
 * can be given to set the start position of the search. */
DSDECL(int, MStringFind(MString_t* String, mchar_t Character, int StartIndex));
DSDECL(int, MStringFindReverse(MString_t* String, mchar_t Character, int StartIndex));

/**
 * @brief Find first occurence of the given UTF8 string
 *
 * @param text
 * @param lookFor
 * @return Returns the character index for the start of the found string
 */
DSDECL(int, MStringFindC(MString_t* text, const char* lookFor));

/**
 * @brief Replace string occurences, this function replaces occurence of <SearchFor> string
 * with <ReplaceWith> string. The strings must be of format of UTF8.
 *
 * @param[In] String
 * @param[In] SearchFor
 * @param[In] ReplaceWith
 * @return returns 0 if a match was replaced, otherwise -1.
 */
DSDECL(int, MStringReplaceC(MString_t* String, const char* SearchFor, const char* ReplaceWith));

/* Get character at the given index and 
 * return the character found as UTF32 */
DSDECL(mchar_t, MStringGetCharAt(MString_t* String, int Index));

/* Iterate through a MString, it returns the next
 * character each time untill MSTRING_EOS. Call with Iterator = NULL
 * the first time, it holds the state. And Index = 0. */
DSDECL(mchar_t,    MStringIterate(MString_t* String, char      ** Iterator, size_t* Index));
DSDECL(MString_t*, MStringSubString(MString_t * string, int    index, int length));

DSDECL(size_t,      MStringLength(MString_t * string)); // Number of characters
DSDECL(size_t,      MStringSize(MString_t * string));   // Number of bytes
DSDECL(const char*, MStringRaw(MString_t * string));
DSDECL(size_t,      MStringHash(MString_t* String));

/* MStringCompare
 * Compare two strings with either case-insensitivity or not. 
 * Returns 
 *   - MSTRING_FULL_MATCH           If they are equal
 *   - MSTRING_PARTIAL_MATCH        If they contain same text but one of the strings are longer
 *   - MSTRING_NO_MATCH             If they don't match */
DSDECL(int, MStringCompare(MString_t* String1, MString_t* String2, int IgnoreCase));

/* MStringGetAscii
 * Converts the given MString into Ascii string and stores it
 * in the given buffer. Unicode characters are truncated. */
DSDECL(void,       MStringGetAscii(MString_t *String, char* Buffer, size_t BufferLength));
DSDECL(void,       MStringPrint(MString_t* String));
DSDECL(void,       MStringUpperCase(MString_t* String));
DSDECL(MString_t*, MStringUpperCaseCopy(MString_t* String));
DSDECL(void,       MStringLowerCase(MString_t* String));
DSDECL(MString_t*, MStringLowerCaseCopy(MString_t* String));
_CODE_END

#endif //!__MSTRING_INTERFACE_H__
