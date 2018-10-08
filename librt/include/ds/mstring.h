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

#ifndef _MCORE_STRING_H_
#define _MCORE_STRING_H_

#include <os/osdefs.h>
#include <ds/ds.h>

typedef uint32_t mchar_t;
#define MSTRING_NOT_FOUND		-1          // Returned if an occurence is not found
#define MSTRING_EOS				0xFFFD      // Marker for identifying end of string
#define MSTRING_NO_MATCH		0           // No match was found
#define MSTRING_FULL_MATCH		1           // A full match was found
#define MSTRING_PARTIAL_MATCH	2           // Partial match, means it contains the match

typedef enum _MStringType {
	StrASCII,
	StrUTF8,
	StrUTF16,
	StrUTF32,
	Latin1
} MStringType_t;
struct _MString;
typedef struct _MString MString_t;

_CODE_BEGIN
/* Creates a MString instace from string data
 * The possible string-data types are ASCII, UTF8, UTF16, UTF32
 * and it automatically converts the data to an UTf8 representation
 * and keeps it as UTF8 internally */
CRTDECL(MString_t*, MStringCreate(void* Data, MStringType_t DataType));
CRTDECL(void,       MStringDestroy(MString_t* String));

/* Copies some or all of string data 
 * from Source to Destination, it does NOT append
 * the string, but rather overrides in destination, 
 * if -1 is given in length, it copies the entire Source */
CRTDECL(void, MStringCopy(MString_t* Destination, MString_t* Source, int Length));

/* Append Character to a given string 
 * the character is assumed to be either 
 * ASCII, UTF16 or UTF32 and NOT utf8 */
CRTDECL(void, MStringAppendCharacter(MString_t* String, mchar_t Character));
CRTDECL(void, MStringAppendCharacters(MString_t* String, const char* Characters, MStringType_t DataType));
CRTDECL(void, MStringAppendString(MString_t* Destination, MString_t* String));
CRTDECL(void, MStringAppendInt32(MString_t* String, int32_t Value));
CRTDECL(void, MStringAppendUInt32(MString_t* String, uint32_t Value));
CRTDECL(void, MStringAppendHex32(MString_t* String, uint32_t Value));
CRTDECL(void, MStringAppendInt64(MString_t* String, int64_t Value));
CRTDECL(void, MStringAppendUInt64(MString_t* String, uint64_t Value));
CRTDECL(void, MStringAppendHex64(MString_t* String, uint64_t Value));

/* MStringFind/MStringFindReverse
 * Retrieves the index of the first occurence of the given character. Optionally a start-index
 * can be given to set the start position of the search. */
CRTDECL(int, MStringFind(MString_t* String, mchar_t Character, int StartIndex));
CRTDECL(int, MStringFindReverse(MString_t* String, mchar_t Character, int StartIndex));
CRTDECL(int, MStringFindCString(MString_t* String, const char* Chars));

/* Get character at the given index and 
 * return the character found as UTF32 */
CRTDECL(mchar_t, MStringGetCharAt(MString_t* String, int Index));

/* Iterate through a MString, it returns the next
 * character each time untill MSTRING_EOS. Call with Iterator = NULL
 * the first time, it holds the state. And Index = 0. */
CRTDECL(mchar_t, MStringIterate(MString_t* String, char** Iterator, size_t* Index));

/* Substring - build substring from the given mstring
 * starting at Index with the Length. If the length is -1
 * it takes the rest of string */
CRTDECL(MString_t*, MStringSubString(MString_t* String, int Index, int Length));
CRTDECL(int,        MStringReplace(MString_t* String, const char* SearchFor, const char* ReplaceWith));

/* MStringLength
 * Returns the number of individual characters in the string. */
CRTDECL(size_t, MStringLength(MString_t* String));

/* MStringSize
 * Returns the number of bytes used by the string. */
CRTDECL(size_t,         MStringSize(MString_t* String));
CRTDECL(const char*,    MStringRaw(MString_t* String));
CRTDECL(size_t,         MStringHash(MString_t* String));

/* MStringCompare
 * Compare two strings with either case-insensitivity or not. 
 * Returns 
 *   - MSTRING_FULL_MATCH           If they are equal
 *   - MSTRING_PARTIAL_MATCH        If they contain same text but one of the strings are longer
 *   - MSTRING_NO_MATCH             If they don't match */
CRTDECL(int, MStringCompare(MString_t* String1, MString_t* String2, int IgnoreCase));

/* MStringGetAscii
 * Converts the given MString into Ascii string and stores it
 * in the given buffer. Unicode characters are truncated. */
CRTDECL(void,       MStringGetAscii(MString_t *String, char* Buffer, size_t BufferLength));
CRTDECL(void,       MStringPrint(MString_t* String));
CRTDECL(void,       MStringUpperCase(MString_t* String));
CRTDECL(MString_t*, MStringUpperCaseCopy(MString_t* String));
CRTDECL(void,       MStringLowerCase(MString_t* String));
CRTDECL(MString_t*, MStringLowerCaseCopy(MString_t* String));
_CODE_END

#endif //!_MCORE_STRING_H_
