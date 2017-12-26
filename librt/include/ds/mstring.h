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

/* Includes */
#include <stdint.h>
#include <crtdefs.h>

/* The overall datastructure 
 * header for shared implementations 
 * between c-lib and kern */
#include <ds/ds.h>

/* This describes the type used for
 * chars in mstring (at max), which should 
 * be 32 bit */
typedef uint32_t mchar_t;

/* Used for char-searches and string-searches 
 * methods that return int */
#define MSTRING_NOT_FOUND		-1

/* Used for the MStringIterate function to indicate end of string */
#define MSTRING_EOS				0xFFFD

/* Used by MStringCompare, if the string match 100%
 * the MSTRING_FULL_MATCH is returned, if they match,
 * but one of the string has more content afterwards,
 * a partial match is returned, otherwise NO_MATCH is returned */
#define MSTRING_NO_MATCH		0
#define MSTRING_FULL_MATCH		1
#define MSTRING_PARTIAL_MATCH	2

/* String Types 
 * Used by MStringCreate to tell us
 * which type of data we are feeding the
 * constructor */
typedef enum _MStringType {
	StrASCII,
	StrUTF8,
	StrUTF16,
	StrUTF32,
	Latin1
} MStringType_t;

/* Structures 
 * The MString structure */
struct _MString;
typedef struct _MString MString_t;

_CODE_BEGIN
/* Creates a MString instace from string data
 * The possible string-data types are ASCII, UTF8, UTF16, UTF32
 * and it automatically converts the data to an UTf8 representation
 * and keeps it as UTF8 internally */
CRTDECL(MString_t*, MStringCreate(void *Data, MStringType_t DataType));

/* Destroys the string and frees any resourec
 * allocated by the structure */
CRTDECL(void, MStringDestroy(MString_t *String));

/* Copies some or all of string data 
 * from Source to Destination, it does NOT append
 * the string, but rather overrides in destination, 
 * if -1 is given in length, it copies the entire Source */
CRTDECL(void, MStringCopy(MString_t *Destination, MString_t *Source, int Length));

/* Append Character to a given string 
 * the character is assumed to be either 
 * ASCII, UTF16 or UTF32 and NOT utf8 */
CRTDECL(void, MStringAppendCharacter(MString_t *String, mchar_t Character));

/* Appends raw string data to a 
 * given mstring, you must indicate what format
 * the raw string is of so it converts correctly. */
CRTDECL(void, MStringAppendCharacters(MString_t *String, __CONST char *Characters, MStringType_t DataType));

/* Append MString to MString 
 * This appends the given String the destination string */
CRTDECL(void, MStringAppendString(MString_t *Destination, MString_t *String));

CRTDECL(void, MStringAppendInt32(MString_t *String, int32_t Value));
CRTDECL(void, MStringAppendUInt32(MString_t *String, uint32_t Value));
CRTDECL(void, MStringAppendHex32(MString_t *String, uint32_t Value));

CRTDECL(void, MStringAppendInt64(MString_t *String, int64_t Value));
CRTDECL(void, MStringAppendUInt64(MString_t *String, uint64_t Value));
CRTDECL(void, MStringAppendHex64(MString_t *String, uint64_t Value));

/* Find first/last occurence of the given character in the given string. 
 * The character given to this function should be UTF8
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
CRTDECL(int, MStringFind(MString_t *String, mchar_t Character));
CRTDECL(int, MStringFindReverse(MString_t *String, mchar_t Character));

/* MStringFindCString
 * Find first occurence of the given UTF8 string
 * in the given string. This does not accept UTF16 or UTF32.
 * returns the index if found, otherwise -1 */
CRTDECL(int, MStringFindCString(
    _In_ MString_t  *String,
    _In_ const char *Chars));

/* Get character at the given index and 
 * return the character found as UTF32 */
CRTDECL(mchar_t, MStringGetCharAt(MString_t *String, int Index));

/* Iterate through a MString, it returns the next
 * character each time untill MSTRING_EOS. Call with Iterator = NULL
 * the first time, it holds the state. And Index = 0. */
CRTDECL(mchar_t, MStringIterate(MString_t *String, char **Iterator, size_t *Index));

/* Substring - build substring from the given mstring
 * starting at Index with the Length. If the length is -1
 * it takes the rest of string */
CRTDECL(MString_t*, MStringSubString(MString_t *String, int Index, int Length));

/* MStringReplace
 * Replace string occurences, this function replaces occurence of <SearchFor> string 
 * with <ReplaceWith> string. The strings must be of format of UTF8. returns MSTRING_NO_MATCH
 * on any errors. */
CRTDECL(int, MStringReplace(
    _In_ MString_t  *String,
    _In_ const char *SearchFor,
    _In_ const char *ReplaceWith));

/* MStringLength
 * Get's the number of characters in a mstring and not the actual byte length. */
CRTDECL(size_t, MStringLength(
    _In_ MString_t *String));

/* MStringSize
 * Retrieves the number of bytes used in the given mstring */
CRTDECL(size_t, MStringSize(
    _In_ MString_t *String));

/* MStringRaw
 * Returns the data buffer pointer to the string data. */
CRTDECL(const char*, MStringRaw(
    _In_ MString_t *String));

/* Generate hash of a mstring
 * the hash will be either 32/64 depending
 * on the size of architecture */
CRTDECL(size_t, MStringHash(MString_t *String));

/* Compare two strings with either case-ignore or not. 
 * Returns MSTRING_FULL_MATCH if they are equal, or
 * MSTRING_PARTIAL_MATCH if they contain same text 
 * but one of the strings are longer. Returns MSTRING_NO_MATCH
 * if not match */
CRTDECL(int, MStringCompare(MString_t *String1, MString_t *String2, int IgnoreCase));

/* MStringGetAscii
 * Converts the given MString into Ascii string and stores it
 * in the given buffer. Unicode characters are truncated. */
CRTDECL(void, MStringGetAscii(
    _In_ MString_t  *String,
    _Out_ char      *Buffer,
    _In_ size_t      BufferLength));

/* MStringPrint
 * Writes the string to stdout. */
CRTDECL(void, MStringPrint(
    _In_ MString_t *String));

/* Casing */
CRTDECL(void, MStringUpperCase(MString_t *String));
CRTDECL(MString_t*, MStringUpperCaseCopy(MString_t *String));

CRTDECL(void, MStringLowerCase(MString_t *String));
CRTDECL(MString_t*, MStringLowerCaseCopy(MString_t *String));
_CODE_END

#endif //!_MCORE_STRING_H_