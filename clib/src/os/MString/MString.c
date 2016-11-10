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

/* Append Character to a given string 
 * the character is assumed to be either ASCII, UTF16 or UTF32
 * and NOT utf8 */
void MStringAppendChar(MString_t *String, mchar_t Character)
{
	/* Vars */
	uint8_t *BufPtr = NULL;
	uint32_t cLen = 0;
	int Itr = 0;

	/* Sanity */ 
	if ((String->Length + Utf8ByteSizeOfCharacterInUtf8(Character)) >= String->MaxLength)
	{
		/* Expand */
		void *nDataBuffer = dsalloc(String->MaxLength + MSTRING_BLOCK_SIZE);
		memset(nDataBuffer, 0, String->MaxLength + MSTRING_BLOCK_SIZE);

		/* Copy old data over */
		memcpy(nDataBuffer, String->Data, String->Length);

		/* Free */
		dsfree(String->Data);

		/* Set new */
		String->MaxLength += MSTRING_BLOCK_SIZE;
		String->Data = nDataBuffer;
	}

	/* Cast */
	BufPtr = (uint8_t*)String->Data;

	/* Loop to end of string */
	while (BufPtr[Itr])
		Itr++;

	/* Append */
	Utf8ConvertCharacterToUtf8(Character, (void*)&BufPtr[Itr], &cLen);

	/* Null-terminate */
	BufPtr[Itr + cLen] = '\0';

	/* Done? */
	String->Length += cLen;
}

/* Find first occurence of the given UTF8 string
 * in the given string. This does not accept UTF16 or UTF32.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFindChars(MString_t *String, const char *Chars)
{
	/* Loop vars */
	int Result = 0;
	char *DataPtr = (char*)String->Data;
	int iSource = 0, iDest = 0;

	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return MSTRING_NOT_FOUND;

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

/* Substring - build substring from the given mstring
 * starting at Index with the Length. If the length is -1
 * it takes the rest of string */
MString_t *MStringSubString(MString_t *String, int Index, int Length)
{
	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return NULL;

	/* More Sanity */
	if (Index > (int)String->Length
		|| ((((Index + Length) > (int)String->Length)) && Length != -1))
		return NULL;

	/* Vars */
	int cIndex = 0, i = 0, lasti = 0;
	char *DataPtr = (char*)String->Data;
	size_t DataLength = 0;

	/* Count size */
	while (DataPtr[i]) {

		/* Save position */
		lasti = i;

		/* Get next */
		Utf8GetNextCharacterInString(DataPtr, &i);

		/* Sanity */
		if (cIndex >= Index
			&& ((cIndex < (Index + Length))
				|| Length == -1))
		{
			/* Lets count */
			DataLength += (i - lasti);
		}

		/* Increase */
		cIndex++;
	}

	/* Increase for null terminator */
	DataLength++;

	/* Allocate */
	MString_t *SubString = (MString_t*)dsalloc(sizeof(MString_t));

	/* Calculate Length */
	size_t BlockCount = (DataLength / MSTRING_BLOCK_SIZE) + 1;

	/* Set */
	SubString->Data = dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
	SubString->Length = DataLength;
	SubString->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;

	/* Zero it */
	memset(SubString->Data, 0, BlockCount * MSTRING_BLOCK_SIZE);

	/* Now, iterate again, but copy data over */
	void *NewDataPtr = SubString->Data;
	i = 0;
	lasti = 0;
	cIndex = 0;

	while (DataPtr[i]) {

		/* Save position */
		lasti = i;

		/* Get next */
		mchar_t CharIndex =
			Utf8GetNextCharacterInString(DataPtr, &i);

		/* Sanity */
		if (cIndex >= Index
			&& ((cIndex < (Index + Length))
			|| Length == -1))
		{
			/* Lets copy */
			memcpy(NewDataPtr, (void*)&CharIndex, (i - lasti));
			
			/* Advance pointer */
			size_t DataPtrAddr = (size_t)NewDataPtr;
			DataPtrAddr += (i - lasti);
			NewDataPtr = (void*)DataPtrAddr;
		}

		/* Increase */
		cIndex++;
	}

	/* Done! */
	return SubString;
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
	if (NewLen > String->MaxLength)
	{
		/* Calculate new alloc size */
		uint32_t BlockCount = (NewLen / MSTRING_BLOCK_SIZE) + 1;

		/* Expand */
		void *nDataBuffer = dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
		memset(nDataBuffer, 0, BlockCount * MSTRING_BLOCK_SIZE);

		/* Free */
		dsfree(String->Data);

		/* Set new */
		String->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;
		String->Data = nDataBuffer;
	}

	/* Copy over */
	memcpy(String->Data, TempPtr, NewLen);
	String->Length = NewLen;

	/* Free */
	dsfree(TempPtr);
}

/* Compare two strings with either case-ignore or not. 
 * Returns MSTRING_FULL_MATCH if they are equal, or
 * MSTRING_PARTIAL_MATCH if they contain same text 
 * but one of the strings are longer. Returns MSTRING_NO_MATCH
 * if not match */
int MStringCompare(MString_t *String1, MString_t *String2, int IgnoreCase)
{
	/* If ignore case we use IsAlpha on their asses and then tolower */
	/* Loop vars */
	char *DataPtr1 = (char*)String1->Data;
	char *DataPtr2 = (char*)String2->Data;
	int i1 = 0, i2 = 0;

	/* Iterate */
	while (DataPtr1[i1]
		&& DataPtr2[i2])
	{
		/* Get characters */
		mchar_t First =
			Utf8GetNextCharacterInString(DataPtr1, &i1);
		mchar_t Second =
			Utf8GetNextCharacterInString(DataPtr2, &i2);

		/* Ignore case? - Only on ASCII alpha-chars */
		if (IgnoreCase)
		{
			/* Sanity */
			if (First < 0x80
				&& isalpha(First))
				First = tolower((uint8_t)First);
			if (Second < 0x80
				&& isalpha(Second))
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
