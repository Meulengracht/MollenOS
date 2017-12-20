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
#include "mstringprivate.h"

/* Find first occurence of the given UTF8 string
 * in the given string. This does not accept UTF16 or UTF32.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFindChars(MString_t *String, const char *Chars)
{
	/* Loop vars */
	int Result = 0;
	char *DataPtr = (char*)String->Data;
	int iSource = 0, iDest = 0;

	// Sanitize input
	if (String->Data == NULL || String->Length == 0) {
        return MSTRING_NO_MATCH;
    }
    
	/* Iterate */
	while (DataPtr[iSource]) {

		/* Get next in source */
		mchar_t SourceCharacter =
			Utf8GetNextCharacterInString(DataPtr, &iSource);

		/* Get first in dest */
		iDest = 0;
		mchar_t DestCharacter =
			Utf8GetNextCharacterInString(Chars, &iDest);

		/* Is it equal? */
		if (SourceCharacter == DestCharacter)
		{
			/* Save pos */
			int iTemp = iSource;

			/* Validate */
			while (DataPtr[iTemp] && Chars[iDest] &&
				SourceCharacter == DestCharacter)
			{ 
				/* Get next */
				SourceCharacter = Utf8GetNextCharacterInString(DataPtr, &iTemp);
				DestCharacter = Utf8GetNextCharacterInString(DataPtr, &iDest);
			}

			/* Are they still equal? */
			if (SourceCharacter == DestCharacter)
			{
				/* Sanity, only ok if we ran out of chars in Chars */
				if (!Chars[iDest])
					return Result;
			}

			/* No, no, not a match */
		}

		/* Othewise, keep searching */
		Result++;
	}

	/* No entry */
	return MSTRING_NOT_FOUND;
}

/* Replace string occurences,
 * this function replaces occurence of <Old> string 
 * with <New> string. The strings must be of format of UTF8 */
void MStringReplace(MString_t *String, const char *Old, const char *New)
{
	/* Vars */
	uint8_t *TempBuffer = NULL, *TempPtr;
	char *DataPtr = (char*)String->Data;
	int iSource = 0, iDest = 0, iLast = 0;
	size_t NewLen = strlen(New);

	/* Sanity 
	 * If no occurences, then forget it */
	if (MStringFindChars(String, Old) == MSTRING_NOT_FOUND)
		return;

	/* We need a temporary buffer */
	TempPtr = TempBuffer = (uint8_t*)dsalloc(1024);
	memset(TempBuffer, 0, 1024);

	/* Iterate */
	while (DataPtr[iSource]) {

		/* Save */
		iLast = iSource;

		/* Get next in source */
		mchar_t SourceCharacter =
			Utf8GetNextCharacterInString(DataPtr, &iSource);

		/* Get first in dest */
		iDest = 0;
		mchar_t DestCharacter =
			Utf8GetNextCharacterInString(Old, &iDest);

		/* Is it equal? */
		if (SourceCharacter == DestCharacter)
		{
			/* Save pos */
			int iTemp = iSource;

			/* Validate */
			while (DataPtr[iTemp] && Old[iDest] &&
				SourceCharacter == DestCharacter)
			{
				/* Get next */
				SourceCharacter = Utf8GetNextCharacterInString(DataPtr, &iTemp);
				DestCharacter = Utf8GetNextCharacterInString(DataPtr, &iDest);
			}

			/* Are they still equal? */
			if (SourceCharacter == DestCharacter)
			{
				/* Sanity, only ok if we ran out of chars in Chars */
				if (!Old[iDest])
				{
					/* Write new to temp buffer */
					memcpy(TempBuffer, New, NewLen);
					TempBuffer += NewLen;

					/* continue */
					continue;
				}
			}
		}

		/* Copy it */
		memcpy(TempBuffer, &DataPtr[iLast], (iSource - iLast));
		TempBuffer += (iSource - iLast);
	}

	/* Done! Reconstruct */
	NewLen = strlen((const char*)TempPtr);

	/* Sanity */
	if (NewLen > String->MaxLength) {
		MStringResize(String, NewLen);
	}

	/* Copy over */
	memcpy(String->Data, TempPtr, NewLen);
	String->Length = NewLen;

	/* Free */
	dsfree(TempPtr);
}

/* Converts mstring-data to ASCII, if a character is non-ascii
 * the character is ignored. */
void MStringToASCII(MString_t *String, void *Buffer)
{
	/* State Vars */
	char *DataPtr = (char*)String->Data;
	char *OutB = (char*)Buffer;
	int i = 0, j = 0, Count = 0;
	uint32_t Value = 0;

	while (DataPtr[i])
	{
		/* Convert */
		unsigned char uChar = (unsigned char)DataPtr[i];

		/* Easy */
		if (uChar < 0x80)
		{
			OutB[j] = DataPtr[i];
			j++;
		}
		/* Lead Byte? */
		else if ((uChar & 0xC0) == 0xC0)
		{
			/* Wtf, multiple leads? */
			if (Count > 0)
				return;

			if ((uChar & 0xF8) == 0xF0) {
				Value = uChar & 0x07;
				Count = 3;
			}
			else if ((uChar & 0xF0) == 0xE0) {
				Value = uChar & 0x0f;
				Count = 2;
			}
			else if ((uChar & 0xE0) == 0xC0) {
				Value = uChar & 0x1f;
				Count = 1;
			}
			else {
				/* Invalid byte */
				return;
			}
		}
		else
		{
			Value <<= 6;
			Value |= uChar & 0x3f;
			if (--Count <= 0) {

				/* A single byte val we can handle */
				if (Value <= 0xFF)
				{
					OutB[j] = DataPtr[i];
					j++;
				}
			}
		}

		/* Next byte */
		i++;
	}
}
