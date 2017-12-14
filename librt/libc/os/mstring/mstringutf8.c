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

/* Helpers */
#define IsUTF8(Character) (((Character) & 0xC0) == 0x80)

/* Offset Table */
uint32_t GlbUtf8Offsets[6] = {
	0x00000000UL, 0x00003080UL, 0x000E2080UL,
	0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

/* Extra Byte Table */
char GlbUtf8ExtraBytes[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5
};

/* Converts a single char (ASCII, UTF16, UTF32) to UTF8 
 * and returns the number of bytes the new utf8 
 * 'string' takes up. Returns 0 if conversion was good */
int Utf8ConvertCharacterToUtf8(mchar_t Character, void* oBuffer, size_t *Length)
{
	/* Encode Buffer */
	char TmpBuffer[10] = { 0 };
	char* BufPtr = &TmpBuffer[0];

	size_t NumBytes = 0;
	int Error = 0;

	if (Character <= 0x7F)  /* 0XXX XXXX one byte */
    {
		TmpBuffer[0] = (char)Character;
		NumBytes = 1;
    }
	else if (Character <= 0x7FF)  /* 110X XXXX  two bytes */
    {
		TmpBuffer[0] = (char)(0xC0 | (Character >> 6));
		TmpBuffer[1] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 2;
    }
	else if (Character <= 0xFFFF)  /* 1110 XXXX  three bytes */
    {
		TmpBuffer[0] = (char)(0xE0 | (Character >> 12));
		TmpBuffer[1] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[2] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 3;
        
		/* Sanity no special characters */
		if (Character == 0xFFFE || Character == 0xFFFF)
			Error = 1;
    }
	else if (Character <= 0x1FFFFF)  /* 1111 0XXX  four bytes */
    {
		TmpBuffer[0] = (char)(0xF0 | (Character >> 18));
		TmpBuffer[1] = (char)(0x80 | ((Character >> 12) & 0x3F));
		TmpBuffer[2] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[3] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 4;
        
		if (Character > 0x10FFFF)
			Error = 1;
    }
	else if (Character <= 0x3FFFFFF)  /* 1111 10XX  five bytes */
    {
		TmpBuffer[0] = (char)(0xF8 | (Character >> 24));
		TmpBuffer[1] = (char)(0x80 | (Character >> 18));
		TmpBuffer[2] = (char)(0x80 | ((Character >> 12) & 0x3F));
		TmpBuffer[3] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[4] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 5;
		Error = 1;
    }
	else if (Character <= 0x7FFFFFFF)  /* 1111 110X  six bytes */
    {
		TmpBuffer[0] = (char)(0xFC | (Character >> 30));
		TmpBuffer[1] = (char)(0x80 | ((Character >> 24) & 0x3F));
		TmpBuffer[2] = (char)(0x80 | ((Character >> 18) & 0x3F));
		TmpBuffer[3] = (char)(0x80 | ((Character >> 12) & 0x3F));
		TmpBuffer[4] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[5] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 6;
		Error = 1;
    }
	else
		Error = 1;

	/* Write buffer only if it's a valid byte sequence */
	if (!Error && oBuffer != NULL)
		memcpy(oBuffer, BufPtr, NumBytes);

	/* We want the length */
	*Length = NumBytes;
	
	/* Sanity */
	if (Error)
		return -1;
	else
		return 0;
 }

/* Bytes used by given (ASCII, UTF16, UTF32) character in UTF-8 Encoding
 * If 0 is returned the character was invalid */
size_t Utf8ByteSizeOfCharacterInUtf8(mchar_t Character)
{
	/* Simple length check */
	if (Character < 0x80)
		return 1;
	else if (Character < 0x800)
		return 2;
	else if (Character < 0x10000)
		return 3;
	else if (Character < 0x110000)
		return 4;

	/* Invalid! */
	return 0;
}

/* Reads the next utf-8 sequence out of a string, updating an index 
 * the index keeps track of how many characters into the string
 * we are. Returns MSTRING_EOS on errors */
mchar_t Utf8GetNextCharacterInString(const char *Str, int *Index)
{
	/* We'll need these to keep track */
	mchar_t Character = MSTRING_EOS;
	int Size = 0, lIndex = *Index;

	/* Sanitize that index is within bounds 
	 * otherwise the index is invalid 
	 * and string is done */
	if (Str == NULL || strlen(Str) <= (size_t)lIndex) {
		goto Done;
	}

	/* Iterate while string is not EOS and 
	 * while the given character is UTF8 flagged */
	while (Str[lIndex] && IsUTF8(Str[lIndex])) {
		/* Add byte to character and advance */
		Character <<= 6;
		Character += (unsigned char)Str[lIndex++];
		Size++;
	}

	/* Sanitize size
	 * If size is 0, but string is valid 
	 * then the first character was below 0x80 */
	if (Size == 0 && Str[lIndex]) {
		Character = (mchar_t)Str[lIndex];
		lIndex++; Size++;
	}

	/* Modify by the UTF8 offset table 
	 * but do a sanity check again */
	if (Size != 0) {
		Character -= GlbUtf8Offsets[Size - 1];
	}

Done:
	/* Update index */
	*Index = lIndex;

	/* Simply return the character */
	return Character;
}

/* Character Count of UTF8-String 
 * Returns the size of an UTF8 string in char-count 
 * this is used to tell how long strings are */
size_t Utf8CharacterCountInString(const char *Str)
{
	/* The count of characters 
	 * and index tracker */
	size_t Length = 0;
	int Index = 0;

	/* Sanitize the parameters */
	if (Str == NULL) {
		return 0;
	}

	/* Keep iterating untill end */
	while (Utf8GetNextCharacterInString(Str, &Index) != MSTRING_EOS) {
		Length++;
	}

	/* Done! */
	return Length;
}

/* Byte Count of UTF8-String 
 * Returns the size of an UTF8 string in bytes 
 * this is used to tell how long strings are */
size_t Utf8ByteCountInString(const char *Str)
{
	/* Sanitize the parameters */
	if (Str == NULL) {
		return 0;
	}

	/* Simply use strlen for counting */
	return strlen(Str);
}
