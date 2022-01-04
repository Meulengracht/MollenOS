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

mchar_t MStringGetCharAt(MString_t *String, int Index)
{
	char* sPtr   = (char*)String->Data;
	int   sIndex = 0, Itr = 0;

	if (String->Data == NULL || String->Length == 0)
		return MSTRING_EOS;

	while (sPtr[sIndex] && sIndex < (int)String->Length) {
		mchar_t Character = Utf8GetNextCharacterInString(sPtr, &sIndex);
		if (Character == MSTRING_EOS) {
			break;
		}

		if (Itr == Index) {
			return Character;
		}
		Itr++;
	}
	return MSTRING_EOS;
}

mchar_t MStringIterate(MString_t *String, char **Iterator, size_t *Index)
{
	uint8_t *Ptr = NULL;
	mchar_t Character = MSTRING_EOS;
	int Overlong = 0;
	int Underflow = 0;
	int Left = 0;

	/* Sanitize */
	if (String->Data == NULL || String->Length == 0) {
		return MSTRING_EOS;
	}

	/* Fill in first call to iterator */
	if (*Iterator == NULL) {
		*Iterator = (char*)String->Data;
		*Index = String->Length;
	}

	Ptr = *(uint8_t**)Iterator;
	if (Ptr[0] >= 0xFC) {
		if ((Ptr[0] & 0xFE) == 0xFC) {
			if (Ptr[0] == 0xFC && (Ptr[1] & 0xFC) == 0x80) {
				Overlong = 1;
			}

			Character = (mchar_t)(Ptr[0] & 0x01);
			Left = 5;
		}
	}
	else if (Ptr[0] >= 0xF8) {
		if ((Ptr[0] & 0xFC) == 0xF8) {
			if (Ptr[0] == 0xF8 && (Ptr[1] & 0xF8) == 0x80) {
				Overlong = 1;
			}

			Character = (mchar_t)(Ptr[0] & 0x03);
			Left = 4;
		}
	}
	else if (Ptr[0] >= 0xF0) {
		if ((Ptr[0] & 0xF8) == 0xF0) {
			if (Ptr[0] == 0xF0 && (Ptr[1] & 0xF0) == 0x80) {
				Overlong = 1;
			}

			Character = (mchar_t)(Ptr[0] & 0x07);
			Left = 3;
		}
	}
	else if (Ptr[0] >= 0xE0) {
		if ((Ptr[0] & 0xF0) == 0xE0) {
			if (Ptr[0] == 0xE0 && (Ptr[1] & 0xE0) == 0x80) {
				Overlong = 1;
			}

			Character = (mchar_t)(Ptr[0] & 0x0F);
			Left = 2;
		}
	}
	else if (Ptr[0] >= 0xC0) {
		if ((Ptr[0] & 0xE0) == 0xC0) {
			if ((Ptr[0] & 0xDE) == 0xC0) {
				Overlong = 1;
			}

			Character = (mchar_t)(Ptr[0] & 0x1F);
			Left = 1;
		}
	}
	else {
		if ((Ptr[0] & 0x80) == 0x00) {
			Character = (mchar_t)Ptr[0];
		}
	}

	/* Increament sources */
	++*Iterator;
	--*Index;

	/* Build the character */
	while (Left > 0 && *Index > 0) {
		++Ptr;
		if ((Ptr[0] & 0xC0) != 0x80) {
			Character = MSTRING_EOS;
			break;
		}

		/* Append data */
		Character <<= 6;
		Character |= (Ptr[0] & 0x3F);

		/* Next */
		++*Iterator;
		--*Index;
		--Left;
	}

	/* Sanity */
	if (Left > 0) {
		Underflow = 1;
	}

	/* Technically overlong sequences are invalid and should not be interpreted.
	However, it doesn't cause a security risk here and I don't see any harm in
	displaying them. The application is responsible for any other side effects
	of allowing overlong sequences (e.g. string compares failing, etc.)
	See bug 1931 for sample input that triggers this.
	*/
    if (Overlong) {
        // return MSTRING_INVALID;
    }

	if (Underflow == 1 ||
		(Character >= 0xD800 && Character <= 0xDFFF) ||
		(Character == 0xFFFE || Character == 0xFFFF) || Character > 0x10FFFF) {
		Character = MSTRING_EOS;
	}


	return Character;
}
