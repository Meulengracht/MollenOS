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

/* Includes 
 * - System */
#include "MStringPrivate.h"

/* Find first occurence of the given character in the given string. 
 * The character given to this function should be UTF8
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFind(MString_t *String, mchar_t Character)
{
	/* Loop vars */
	char *DataPtr = (char*)String->Data;
	int Result = 0;
	int i = 0;

	/* Sanitize parameters */
	if (String->Data == NULL
		|| String->Length == 0) {
		return MSTRING_NOT_FOUND;
	}

	/* Iterate */
	while (DataPtr[i]) {

		/* Get next character in 
		 * our string */
		mchar_t NextCharacter =
			Utf8GetNextCharacterInString(DataPtr, &i);

		/* Sanitize that 
		 * we haven't reached end or error */
		if (NextCharacter == MSTRING_EOS) {
			return MSTRING_NOT_FOUND;
		}

		/* Sanitize if we actually
		 * have found the character */
		if (NextCharacter == Character) {
			return Result;
		}

		/* Othewise, keep searching */
		Result++;
	}

	/* No entry */
	return MSTRING_NOT_FOUND;
}

/* Find last occurence of the given character in the given string. 
 * The character given to this function should be UTF8
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFindReverse(MString_t *String, mchar_t Character)
{
	/* Loop vars */
	int Result = 0, LastOccurrence = MSTRING_NOT_FOUND;
	char *DataPtr = (char*)String->Data;
	int i = 0;

	/* Sanitize parameters */
	if (String->Data == NULL
		|| String->Length == 0) {
		return LastOccurrence;
	}
	
	/* Iterate */
	while (DataPtr[i]) {

		/* Get next character in
		 * our string */
		mchar_t NextCharacter =
			Utf8GetNextCharacterInString(DataPtr, &i);

		/* Sanitize that
		 * we haven't reached end or error */
		if (NextCharacter == MSTRING_EOS) {
			return MSTRING_NOT_FOUND;
		}

		/* Sanitize if we actually
		 * have found the character */
		if (NextCharacter == Character) {
			LastOccurrence = Result;
		}

		/* Othewise, keep searching */
		Result++;
	}

	/* No entry */
	return LastOccurrence;
}
