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

/* Find first occurence of the given character in the given string. 
 * The character given to this function should be UTF8
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFind(MString_t *String, mchar_t Character)
{
	char *StringPtr;
	int Result      = 0;
	int i           = 0;

	if (String == NULL || String->Data == NULL || String->Length == 0) {
		return MSTRING_NOT_FOUND;
	}
	StringPtr = (char*)String->Data;

	while (i < String->Length) {
		mchar_t NextCharacter = Utf8GetNextCharacterInString(StringPtr, &i);
		if (NextCharacter == MSTRING_EOS) {
			return MSTRING_NOT_FOUND;
		}
		if (NextCharacter == Character) {
			return Result;
		}
		Result++;
	}
	return MSTRING_NOT_FOUND;
}

/* Find last occurence of the given character in the given string. 
 * The character given to this function should be UTF8
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFindReverse(MString_t* String, mchar_t Character)
{
	char *StringPtr;
	int LastOccurrence  = MSTRING_NOT_FOUND;
	int Result          = 0;
	int i               = 0;

	if (String == NULL || String->Data == NULL || String->Length == 0) {
		return LastOccurrence;
	}
	StringPtr = (char*)String->Data;

	while (i < String->Length) {
		mchar_t NextCharacter = Utf8GetNextCharacterInString(StringPtr, &i);
		if (NextCharacter == MSTRING_EOS) {
			return MSTRING_NOT_FOUND;
		}
		if (NextCharacter == Character) {
			LastOccurrence = Result;
		}
		Result++;
	}
	return LastOccurrence;
}
