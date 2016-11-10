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

/* Compare two strings with either case-ignore or not. 
 * Returns MSTRING_FULL_MATCH if they are equal, or
 * MSTRING_PARTIAL_MATCH if they contain same text 
 * but one of the strings are longer. Returns MSTRING_NO_MATCH
 * if not match */
int MStringCompare(MString_t *String1, MString_t *String2, int IgnoreCase)
{
	/* If ignore case we use IsAlpha on their 
	 * asses and then tolower
	 * Loop vars */
	char *DataPtr1 = (char*)String1->Data;
	char *DataPtr2 = (char*)String2->Data;
	int i1 = 0, i2 = 0;

	/* Iterate */
	while (DataPtr1[i1] && DataPtr2[i2])
	{
		/* Get characters */
		mchar_t First =
			Utf8GetNextCharacterInString(DataPtr1, &i1);
		mchar_t Second =
			Utf8GetNextCharacterInString(DataPtr2, &i2);

		/* Sanitize the characters since we might
		 * have gotten either end or error */
		if (First == MSTRING_EOS
			|| Second == MSTRING_EOS) {
			return MSTRING_PARTIAL_MATCH;
		}

		/* Ignore case? - Only on ASCII alpha-chars 
		 * because they are the only ones we can convert
		 * easily without any help lookup tables */
		if (IgnoreCase) {
			if (First < 0x80 && isalpha(First))
				First = tolower((uint8_t)First);
			if (Second < 0x80 && isalpha(Second))
				Second = tolower((uint8_t)Second);
		}

		/* Lets see */
		if (First != Second)
			return MSTRING_NO_MATCH;
	}

	/* Sanity */
	if (DataPtr1[i1] != DataPtr2[i2])
		return MSTRING_NO_MATCH;

	/* Sanity - Length */
	if (strlen(String1->Data) != strlen(String2->Data))
		return MSTRING_PARTIAL_MATCH;

	/* Done - Equal */
	return MSTRING_FULL_MATCH;
}
