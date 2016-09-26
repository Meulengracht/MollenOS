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

/*
 UTF-8 encoding/decoding functions
 Return # of bytes in UTF-8 sequence; result < 0 if illegal sequence
 
 Also see below for UTF-16 encoding/decoding functions
 
 References :
 
 1) UCS Transformation Format 8 (UTF-8):
 ISO/IEC 10646-1:1996 Amendment 2 or ISO/IEC 10646-1:2000 Annex D
 <http://anubis.dkuug.dk/JTC1/SC2/WG2/docs/n1335>
 <http://www.cl.cam.ac.uk/~mgk25/ucs/ISO-10646-UTF-8.html>
 
 Table 4 - Mapping from UCS-4 to UTF-8
 
 2) Unicode standards:
 <http://www.unicode.org/unicode/standard/standard.html>
 
 3) Legal UTF-8 byte sequences:
 <http://www.unicode.org/unicode/uni2errata/UTF-8_Corrigendum.html>
 
 Code point          1st byte    2nd byte    3rd byte    4th byte
 ----------          --------    --------    --------    --------
 U+0000..U+007F      00..7F
 U+0080..U+07FF      C2..DF      80..BF
 U+0800..U+0FFF      E0          A0..BF      80..BF
 U+1000..U+FFFF      E1..EF      80..BF      80..BF
 U+10000..U+3FFFF    F0          90..BF      80..BF      80..BF
 U+40000..U+FFFFF    F1..F3      80..BF      80..BF      80..BF
 U+100000..U+10FFFF  F4          80..8F      80..BF      80..BF
 
 The definition of UTF-8 in Annex D of ISO/IEC 10646-1:2000 also
 allows for the use of five- and six-byte sequences to encode
 characters that are outside the range of the Unicode character
 set; those five- and six-byte sequences are illegal for the use
 of UTF-8 as a transformation of Unicode characters. ISO/IEC 10646
 does not allow mapping of unpaired surrogates, nor U+FFFE and U+FFFF
 (but it does allow other noncharacters).
 
 4) RFC 2279: UTF-8, a transformation format of ISO 10646:
 <http://www.ietf.org/rfc/rfc2279.txt>
 
 5) UTF-8 and Unicode FAQ:
 <http://www.cl.cam.ac.uk/~mgk25/unicode.html>
 
 6) Markus Kuhn's UTF-8 decoder stress test file:
 <http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt>
 
 7) UTF-8 Demo:
 <http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-demo.txt>
 
 8) UTF-8 Sampler:
 <http://www.columbia.edu/kermit/utf8.html>
 
 9) Transformation Format for 16 Planes of Group 00 (UTF-16):
 ISO/IEC 10646-1:1996 Amendment 1 or ISO/IEC 10646-1:2000 Annex C
 <http://anubis.dkuug.dk/JTC1/SC2/WG2/docs/n2005/n2005.pdf>
 <http://www.cl.cam.ac.uk/~mgk25/ucs/ISO-10646-UTF-16.html>
 
 10) RFC 2781: UTF-16, an encoding of ISO 10646:
 <http://www.ietf.org/rfc/rfc2781.txt>
 
 11) UTF-16 invalid surrogate pairs:
 <http://www.unicode.org/unicode/faq/utf_bom.html#16>
 
  UTF-16       UTF-8          UCS-4
  D83F DFF*    F0 9F BF B*    0001FFF*
  D87F DFF*    F0 AF BF B*    0002FFF*
  D8BF DFF*    F0 BF BF B*    0003FFF*
  D8FF DFF*    F1 8F BF B*    0004FFF*
  D93F DFF*    F1 9F BF B*    0005FFF*
  D97F DFF*    F1 AF BF B*    0006FFF*
                  ...
  DBBF DFF*    F3 BF BF B*    000FFFF*
  DBFF DFF*    F4 8F BF B*    0010FFF*
 
  * = E or F
 
  1010  A
  1011  B
  1100  C
  1101  D
  1110  E
  1111  F
 
  */

/* Includes */
#include <ds/mstring.h>

/* C-Library */
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

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

/* Converts a single char (UTF16, UTF32) to UTF8 
 * and returns the number of bytes the new utf8 
 * 'string' takes up. Returns 0 if conversion was good */
int Utf8ConvertChar(mchar_t Character, void* oBuffer, size_t *Length)
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

/* Size of an UTF-8 Character 
 * It returns the size of an utf16 or utf32 character
 * if it would be converted to utf8 bytes */
size_t Utf8CharBytesFromUCS(mchar_t Character)
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
 * we are */
mchar_t Utf8GetNextChar(const char *Str, int *Index)
{
	/* We'll need these to keep track */
	mchar_t Character = 0;
	int Size = 0, lIndex = *Index;

	/* Iterate untill end of character */
	while (Str[lIndex] && IsUTF8(Str[lIndex])) {
		/* Add and get next byte */
		Character <<= 6;
		Character += (unsigned char)Str[lIndex++];
		
		/* Incr count */
		Size++;
	}

	/* Sanity */
	if (Size == 0
		&& Str[lIndex])
	{
		Character = (mchar_t)Str[lIndex];
		lIndex++;
		Size++;
	}

	/* Modify by the UTF8 offset table */
	Character -= GlbUtf8Offsets[Size - 1];

	/* Update */
	*Index = lIndex;

	/* Done */
	return Character;
}

/* Character Length of UTF8-String 
 * Returns the size of an UTF8 string in bytes 
 * this is used to tell how long strings are */
size_t Utf8StringLength(char *s)
{
	/* The count */
	size_t Length = 0;
	int Index = 0;

	/* Keep iterating untill end */
	while (Utf8GetNextChar(s, &Index) != 0)
		Length++;

	/* Done! */
	return Length;
}

/* Creates a MString instace from string data
 * The possible string-data types are ASCII, UTF8, UTF16, UTF32
 * and it automatically converts the data to an UTf8 representation
 * and keeps it as UTF8 internally */
MString_t *MStringCreate(void *Data, MStringType_t DataType)
{
	/* Allocate */
	MString_t *RetStr = (MString_t*)dsalloc(sizeof(MString_t));
	size_t StrLength = 0;

	/* Handle empty */
	if (Data == NULL)
	{
		/* Allocate an empty string */
		RetStr->Data = dsalloc(MSTRING_BLOCK_SIZE);

		/* Memset */
		memset(RetStr->Data, 0, MSTRING_BLOCK_SIZE);

		/* Set rest */
		RetStr->Length = 0;
		RetStr->MaxLength = MSTRING_BLOCK_SIZE;

		/* Done! */
		return RetStr;
	}

	/* Sanity */
	if (DataType == StrASCII
		|| DataType == StrUTF8)
	{
		/* Get length */
		if (DataType == StrUTF8)
			StrLength = strlen((const char*)Data) + 1; 
		else
		{
			/* We have to count manually, one of them might be two bytes */
			char *StrPtr = (char*)Data;
			while (*StrPtr)
			{
				StrLength += Utf8CharBytesFromUCS((mchar_t)*StrPtr);
				StrPtr++;
			}

			/* One more for terminator */
			StrLength++;
		}

		/* Calculate Length */
		size_t BlockCount = (StrLength / MSTRING_BLOCK_SIZE) + 1;

		/* Allocate a buffer */
		RetStr->Data = (void*)dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
		RetStr->Length = StrLength;
		RetStr->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;

		/* Memset it */
		memset(RetStr->Data, 0, BlockCount * MSTRING_BLOCK_SIZE);

		/* Convert */
		if (DataType == StrASCII)
		{
			/* Conversion time! */
			char *StrPtr = (char*)Data;
			void *DataPtr = RetStr->Data;
			while (*StrPtr)
			{
				/* We need this */
				uint32_t Bytes = 0;

				/* Make sure the character is encodable */
				if (!Utf8ConvertChar((uint32_t)*StrPtr, DataPtr, &Bytes))
				{
					size_t DataAddr = (size_t)DataPtr;
					DataAddr += Bytes;
					DataPtr = (void*)DataAddr;
				}

				/* Next */
				StrPtr++;
			}
		}
		else
			memcpy(RetStr->Data, Data, StrLength);
	}
	else if (DataType == Latin1)
	{
		/* Calculate the string length */
		char *SourcePtr = (char*)Data;
		char *DestPtr = NULL;
		StrLength = strlen((const char*)Data) * 2 + 1;

		/* Calculate Length */
		size_t BlockCount = (StrLength / MSTRING_BLOCK_SIZE) + 1;

		/* Allocate a buffer */
		RetStr->Data = (void*)dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
		RetStr->Length = StrLength;
		RetStr->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;

		/* Memset it */
		memset(RetStr->Data, 0, BlockCount * MSTRING_BLOCK_SIZE);
		DestPtr = (char*)RetStr->Data;

		/* Iterate the data given and convert */
		while (*SourcePtr) {
			uint8_t ch = *(uint8_t*)SourcePtr++;
			if (ch <= 0x7F) {
				*DestPtr++ = ch;
			}
			else {
				*DestPtr++ = 0xC0 | ((ch >> 6) & 0x1F);
				*DestPtr++ = 0x80 | (ch & 0x3F);
			}
		}

		/* Null terminate */
		*DestPtr = '\0';
	}
	else
	{
		/* Get length */
		/* We have to count manually */
		if (DataType == StrUTF16)
		{
			uint16_t *StrPtr = (uint16_t*)Data;
			while (*StrPtr)
			{
				StrLength += Utf8CharBytesFromUCS((mchar_t)*StrPtr);
				StrPtr++;
			}

			/* Two more for terminator */
			StrLength += 2;
		}
		else
		{
			mchar_t *StrPtr = (mchar_t*)Data;
			while (*StrPtr)
			{
				StrLength += Utf8CharBytesFromUCS(*StrPtr);
				StrPtr++;
			}

			/* Four more for terminator */
			StrLength += 4;
		}
		
		/* Calculate Length */
		size_t BlockCount = (StrLength / MSTRING_BLOCK_SIZE) + 1;

		/* Allocate a buffer */
		RetStr->Data = (void*)dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
		RetStr->Length = StrLength;
		RetStr->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;

		/* Memset it */
		memset(RetStr->Data, 0, BlockCount * MSTRING_BLOCK_SIZE);

		if (DataType == StrUTF16)
		{
			/* Conversion time! */
			uint16_t *StrPtr = (uint16_t*)Data;
			void *DataPtr = RetStr->Data;
			while (*StrPtr)
			{
				/* We need this */
				uint32_t Bytes = 0;

				/* Make sure the character is encodable */
				if (!Utf8ConvertChar((uint32_t)*StrPtr, DataPtr, &Bytes))
				{
					size_t DataAddr = (size_t)DataPtr;
					DataAddr += Bytes;
					DataPtr = (void*)DataAddr;
				}

				/* Next */
				StrPtr++;
			}
		}
		else
		{
			/* Conversion time! */
			uint32_t *StrPtr = (uint32_t*)Data;
			void *DataPtr = RetStr->Data;
			while (*StrPtr)
			{
				/* We need this */
				uint32_t Bytes = 0;

				/* Make sure the character is encodable */
				if (!Utf8ConvertChar(*StrPtr, DataPtr, &Bytes))
				{
					size_t DataAddr = (size_t)DataPtr;
					DataAddr += Bytes;
					DataPtr = (void*)DataAddr;
				}

				/* Next */
				StrPtr++;
			}
		}
	}

	/* Done */
	return RetStr;
}

/* Destroys the string and frees any resourec
 * allocated by the structure */
void MStringDestroy(MString_t *String)
{
	/* Free buffer */
	dsfree(String->Data);

	/* Free structure */
	dsfree(String);
}

/* Copies some or all of string data 
 * from Source to Destination, it does NOT append
 * the string, but rather overrides in destination, 
 * if -1 is given in length, it copies the entire Source */
void MStringCopy(MString_t *Destination, MString_t *Source, int Length)
{
	/* Sanity */
	if (Destination == NULL
		|| Source == NULL
		|| Source->Length == 0)
		return;

	/* If -1, copy all from source */
	if (Length == -1)
	{
		/* Is destination large enough? */
		if (Source->Length > Destination->MaxLength)
		{
			/* Expand */
			void *nDataBuffer = dsalloc(Source->MaxLength);
			memset(nDataBuffer, 0, Source->MaxLength);

			/* Free */
			if (Destination->Data != NULL)
				dsfree(Destination->Data);

			/* Set new */
			Destination->MaxLength = Source->MaxLength;
			Destination->Data = nDataBuffer;
		}

		/* Copy */
		memcpy(Destination->Data, Source->Data, Source->Length);

		/* Update length */
		Destination->Length = Source->Length;
	}
	else
	{
		/* Calculate byte length to copy */
		char *DataPtr = (char*)Source->Data;
		int Count = Length, Index = 0;

		/* Iterate */
		while (DataPtr[Index] 
			&& Count) {

			/* Get next */
			Utf8GetNextChar(DataPtr, &Index);


			/* Othewise, keep searching */
			Count--;
		}

		/* Is destination large enough? */
		if ((uint32_t)Index > Destination->MaxLength)
		{
			/* Calc size to allocate */
			uint32_t BlockCount = (Index / MSTRING_BLOCK_SIZE) + 1;

			/* Expand */
			void *nDataBuffer = dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
			memset(nDataBuffer, 0, BlockCount * MSTRING_BLOCK_SIZE);

			/* Free */
			if (Destination->Data != NULL)
				dsfree(Destination->Data);

			/* Set new */
			Destination->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;
			Destination->Data = nDataBuffer;
		}

		/* Copy */
		memcpy(Destination->Data, Source->Data, Index);

		/* Null Terminate */
		uint8_t *NullPtr = (uint8_t*)Destination->Data;
		NullPtr[Index] = '\0';
	}
}

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
	if ((String->Length + Utf8CharBytesFromUCS(Character)) >= String->MaxLength)
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
	Utf8ConvertChar(Character, (void*)&BufPtr[Itr], &cLen);

	/* Null-terminate */
	BufPtr[Itr + cLen] = '\0';

	/* Done? */
	String->Length += cLen;
}

/* Appends raw string
 * The string given must be in the format of UTF-8 
 * or ASCII. UTF16 and UTF32 strings must be appended
 * by creating a new MSTRING */
void MStringAppendChars(MString_t *String, const char *Chars)
{
	/* Threat it as it's ASCII */
	uint8_t *BufPtr = NULL;
	int Itr = 0, Index = 0;

	/* Sanity */
	if ((String->Length + strlen(Chars)) >= String->MaxLength)
	{
		/* Calculate */
		size_t BlockCount = ((String->Length + strlen(Chars)) / MSTRING_BLOCK_SIZE) + 1;

		/* Expand */
		void *nDataBuffer = dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
		memset(nDataBuffer, 0, BlockCount * MSTRING_BLOCK_SIZE);

		/* Copy old data over */
		memcpy(nDataBuffer, String->Data, String->Length);

		/* Free */
		dsfree(String->Data);

		/* Set new */
		String->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;
		String->Data = nDataBuffer;
	}

	/* Cast */
	BufPtr = (uint8_t*)String->Data;

	/* Loop to end of string */
	while (BufPtr[Itr])
		Itr++;

	/* Iterate new string */
	while (Chars[Index])
	{
		/* Get character width */
		if (!IsUTF8(Chars[Index])) {
			BufPtr[Itr++] = Chars[Index++];
		}
		else
		{
			/* Get next character */
			mchar_t Character = Utf8GetNextChar(Chars, &Index);
			size_t cLen = 0;
			Utf8ConvertChar(Character, (void*)&BufPtr[Itr], &cLen);
			Itr += cLen;
		}
	}

	/* Null-terminate */
	BufPtr[Itr] = '\0';

	/* Increase */
	String->Length += strlen(Chars);
}

/* Append MString to MString 
 * This appends the given String
 * the destination string */
void MStringAppendString(MString_t *Destination, MString_t *String)
{
	/* Luckily this is UTF8 */
	uint8_t *BufPtr = NULL;

	/* Sanity */
	if ((Destination->Length + String->Length) >= Destination->MaxLength)
	{
		/* Calculate */
		size_t BlockCount = ((Destination->Length + String->Length) / MSTRING_BLOCK_SIZE) + 1;

		/* Expand */
		void *nDataBuffer = dsalloc(BlockCount * MSTRING_BLOCK_SIZE);
		memset(nDataBuffer, 0, BlockCount * MSTRING_BLOCK_SIZE);

		/* Copy old data over */
		memcpy(nDataBuffer, Destination->Data, Destination->Length);

		/* Free */
		dsfree(Destination->Data);

		/* Set new */
		Destination->MaxLength = BlockCount * MSTRING_BLOCK_SIZE;
		Destination->Data = nDataBuffer;
	}

	/* Cast */
	BufPtr = (uint8_t*)Destination->Data;

	/* Loop to end of string */
	while (*BufPtr)
		BufPtr++;

	/* Copy */
	memcpy(BufPtr, String->Data, String->Length);

	/* Update Length */
	Destination->Length += String->Length;
}

/* Find first occurence of the given character (ASCII, UTF16, UTF32)
 * in the given string. This does not accept UTF8 Characters.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFind(MString_t *String, mchar_t Character)
{
	/* Loop vars */
	int Result = 0;
	char *DataPtr = (char*)String->Data;
	int i = 0;

	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return MSTRING_NOT_FOUND;

	/* Iterate */
	while (DataPtr[i]) {

		/* Get next */
		mchar_t CharIndex =
			Utf8GetNextChar(DataPtr, &i);

		/* Is it equal? */
		if (CharIndex == Character)
			return Result;

		/* Othewise, keep searching */
		Result++;
	}

	/* No entry */
	return MSTRING_NOT_FOUND;
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
			Utf8GetNextChar(DataPtr, &iSource);

		/* Get first in dest */
		iDest = 0;
		mchar_t DestCharacter =
			Utf8GetNextChar(Chars, &iDest);

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
				SourceCharacter = Utf8GetNextChar(DataPtr, &iTemp);
				DestCharacter = Utf8GetNextChar(DataPtr, &iDest);
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

/* Find last occurence of the given character (ASCII, UTF16, UTF32)
 * in the given string. This does not accept UTF8 Characters.
 * returns the index if found, otherwise MSTRING_NOT_FOUND */
int MStringFindReverse(MString_t *String, mchar_t Character)
{
	/* Loop vars */
	int Result = 0, LastOccurrence = MSTRING_NOT_FOUND;
	char *DataPtr = (char*)String->Data;
	int i = 0;

	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return LastOccurrence;

	/* Iterate */
	while (DataPtr[i]) {

		/* Get next */
		mchar_t CharIndex =
			Utf8GetNextChar(DataPtr, &i);

		/* Is it equal? */
		if (CharIndex == Character)
			LastOccurrence = Result;

		/* Othewise, keep searching */
		Result++;
	}

	/* No entry */
	return LastOccurrence;
}

/* Get character at the given index and 
 * return the character found as UTF32 */
mchar_t MStringGetCharAt(MString_t *String, int Index)
{
	/* Loop vars */
	char *DataPtr = (char*)String->Data;
	int i = 0, itr = 0;

	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return 0;

	/* Iterate */
	while (DataPtr[i]) {

		/* Get next */
		mchar_t Character =
			Utf8GetNextChar(DataPtr, &i);

		/* Is it equal? */
		if (itr == Index)
			return Character;

		/* Othewise, keep searching */
		itr++;
	}

	/* No entry */
	return 0;
}

/* Iterate through a MString, it returns the next
* character each time untill MSTRING_EOS. Call with Iterator = NULL
* the first time, it holds the state. And Index = 0. */
mchar_t MStringIterate(MString_t *String, char **Iterator, size_t *Index)
{
	/* Variables for iteration */
	const uint8_t *Ptr = *(const uint8_t**)String->Data;
	mchar_t Character = MSTRING_EOS;
	int Overlong = 0;
	int Underflow = 0;
	int Left = 0;

	/* Sanitize */
	if (String->Length == 0) {
		return MSTRING_EOS;
	}

	/* Fill in first call to iterator */
	if (*Iterator == NULL) {
		*Iterator = (char*)String->Data;
		*Index = String->Length;
	}

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

	/*if (overlong) return UNKNOWN_UNICODE;*/
	if (Underflow == 1 ||
		(Character >= 0xD800 && Character <= 0xDFFF) ||
		(Character == 0xFFFE || Character == 0xFFFF) || Character > 0x10FFFF) {
		Character = MSTRING_EOS;
	}

	/* Done! */
	return Character;
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
		Utf8GetNextChar(DataPtr, &i);

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
			Utf8GetNextChar(DataPtr, &i);

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
			Utf8GetNextChar(DataPtr, &iSource);

		/* Get first in dest */
		iDest = 0;
		mchar_t DestCharacter =
			Utf8GetNextChar(Old, &iDest);

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
				SourceCharacter = Utf8GetNextChar(DataPtr, &iTemp);
				DestCharacter = Utf8GetNextChar(DataPtr, &iDest);
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

/* Get's the number of characters in a mstring
 * and not the actual byte length. */
size_t MStringLength(MString_t *String)
{
	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return 0;

	/* Loop vars */
	char *DataPtr = (char*)String->Data;

	/* Done */
	return Utf8StringLength(DataPtr);
}

/* Generate hash of a mstring
 * the hash will be either 32/64 depending
 * on the size of architecture */
size_t MStringHash(MString_t *String)
{
	/* Hash Seed */
	size_t Hash = 5381;
	uint8_t *StrPtr;
	int Char;

	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return 0;

	/* Get a pointer */
	StrPtr = (uint8_t*)String->Data;

	/* Hash */
	while ((Char = tolower(*StrPtr++)) != 0)
		Hash = ((Hash << 5) + Hash) + Char; /* hash * 33 + c */

	/* Done */
	return Hash;
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
			Utf8GetNextChar(DataPtr1, &i1);
		mchar_t Second =
			Utf8GetNextChar(DataPtr2, &i2);

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

#ifdef LIBC_KERNEL
#include <Log.h>
#endif

/* Prints out a mstring to stdout */
void MStringPrint(MString_t *String)
{
#ifdef LIBC_KERNEL
	LogInformation("MSTR", "%s", String->Data);
#else
	/* Simply just print it out*/
	printf("%s\n", String->Data);
#endif
}