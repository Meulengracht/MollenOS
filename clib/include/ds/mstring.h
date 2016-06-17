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

/* Definitions */

/* This describes the type used for
 * chars in mstring (at max), which should 
 * be 32 bit */
typedef uint32_t mchar_t;

/* Used for char-searches and string-searches 
 * methods that return int */
#define MSTRING_NOT_FOUND		-1

/* Used by MStringCompare, if the string match 100%
 * the MSTRING_FULL_MATCH is returned, if they match,
 * but one of the string has more content afterwards,
 * a partial match is returned, otherwise NO_MATCH is returned */
#define MSTRING_NO_MATCH		0
#define MSTRING_FULL_MATCH		1
#define MSTRING_PARTIAL_MATCH	2

/* This is the block size that mstring allocates with
 * this can be tweaked by the user */
#define MSTRING_BLOCK_SIZE		64

/* String Types 
 * Used by MStringCreate to tell us
 * which type of data we are feeding the
 * constructor */
typedef enum _MStringType
{
	StrASCII,
	StrUTF8,
	StrUTF16,
	StrUTF32
} MStringType_t;

/* Structures 
 * The MString structure, it keeps 
 * track of data, length and max length 
 * before next string expansion */
typedef struct _MString
{
	/* String Data
	 * As UTF8 */
	void *Data;
	
	/* Length(s) */
	size_t Length;
	size_t MaxLength;

} MString_t;

/* Creates a MString instace from string data
 * The possible string-data types are ASCII, UTF8, UTF16, UTF32
 * and it automatically converts the data to an UTf8 representation
 * and keeps it as UTF8 internally */
_CRT_EXTERN MString_t *MStringCreate(void *Data, MStringType_t DataType);

/* Destroys the string and frees any resourec
 * allocated by the structure */
_CRT_EXTERN void MStringDestroy(MString_t *String);

/* Copies some or all of string data 
 * from Source to Destination, it does NOT append
 * the string, but rather overrides in destination, 
 * if -1 is given in length, it copies the entire Source */
_CRT_EXTERN void MStringCopy(MString_t *Destination, MString_t *Source, int Length);

/* Append Character to a given string 
 * the character is assumed to be either ASCII, UTF16 or UTF32
 * and NOT utf8 */
_CRT_EXTERN void MStringAppendChar(MString_t *String, mchar_t Character);

/* Appends raw string
 * The string given must be in the format of UTF-8 
 * or ASCII. UTF16 and UTF32 strings must be appended
 * by creating a new MSTRING */
_CRT_EXTERN void MStringAppendChars(MString_t *String, const char *Chars);

/* Append MString to MString 
 * This appends the given String
 * the destination string */
_CRT_EXTERN void MStringAppendString(MString_t *Destination, MString_t *String);

_CRT_EXTERN void MStringAppendInt32(MString_t *String, int32_t Value);
_CRT_EXTERN void MStringAppendUInt32(MString_t *String, uint32_t Value);
_CRT_EXTERN void MStringAppendHex32(MString_t *String, uint32_t Value);

_CRT_EXTERN void MStringAppendInt64(MString_t *String, int64_t Value);
_CRT_EXTERN void MStringAppendUInt64(MString_t *String, uint64_t Value);
_CRT_EXTERN void MStringAppendHex64(MString_t *String, uint64_t Value);

/* Find first occurence of the given character (ASCII, UTF16, UTF32)
 * in the given string. This does not accept UTF8 Characters.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
_CRT_EXTERN int MStringFind(MString_t *String, mchar_t Character);

/* Find first occurence of the given UTF8 string
 * in the given string. This does not accept UTF16 or UTF32.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
_CRT_EXTERN int MStringFindChars(MString_t *String, const char *Chars);

/* Find last occurence of the given character (ASCII, UTF16, UTF32)
 * in the given string. This does not accept UTF8 Characters.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
_CRT_EXTERN int MStringFindReverse(MString_t *String, mchar_t Character);

/* Get character at the given index and 
 * return the character found as UTF32 */
_CRT_EXTERN mchar_t MStringGetCharAt(MString_t *String, int Index);

/* Substring - build substring from the given mstring
 * starting at Index with the Length. If the length is -1
 * it takes the rest of string */
_CRT_EXTERN MString_t *MStringSubString(MString_t *String, int Index, int Length);

/* Replace string occurences,
 * this function replaces occurence of <Old> string 
 * with <New> string. The strings must be of format of UTF8 */
_CRT_EXTERN void MStringReplace(MString_t *String, const char *Old, const char *New);

/* Get's the number of characters in a mstring
 * and not the actual byte length. */
_CRT_EXTERN size_t MStringLength(MString_t *String);

/* Generate hash of a mstring
 * the hash will be either 32/64 depending
 * on the size of architecture */
_CRT_EXTERN size_t MStringHash(MString_t *String);

/* Compare two strings with either case-ignore or not. 
 * Returns MSTRING_FULL_MATCH if they are equal, or
 * MSTRING_PARTIAL_MATCH if they contain same text 
 * but one of the strings are longer. Returns MSTRING_NO_MATCH
 * if not match */
_CRT_EXTERN int MStringCompare(MString_t *String1, MString_t *String2, int IgnoreCase);

/* Converts mstring-data to ASCII, if a character is non-ascii
 * the character is ignored. */
_CRT_EXTERN void MStringToASCII(MString_t *String, void *Buffer);

/* Prints out a mstring to stdout */
_CRT_EXTERN void MStringPrint(MString_t *String);

/* Casing */
_CRT_EXTERN void MStringUpperCase(MString_t *String);
_CRT_EXTERN MString_t *MStringUpperCaseCopy(MString_t *String);

_CRT_EXTERN void MStringLowerCase(MString_t *String);
_CRT_EXTERN MString_t *MStringLowerCaseCopy(MString_t *String);

#endif //!_MCORE_STRING_H_