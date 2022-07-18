/**
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

#define IsUTF8(c) (((c) & 0xC0) == 0x80)

static uint32_t g_utf8OffsetTable[6] = {
	0x00000000UL, 0x00003080UL, 0x000E2080UL,
	0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

static char g_utf8TrailingBytesTable[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5
};

size_t Utf8ByteSizeOfCharacterInUtf8(uint32_t character)
{
	if (character < 0x80)
		return 1;
	else if (character < 0x800)
		return 2;
	else if (character < 0x10000)
		return 3;
	else if (character < 0x110000)
		return 4;
	return 0;
}

size_t Utf8GetSequenceLength(const char* string)
{
    return g_utf8TrailingBytesTable[(unsigned char)string[0]] + 1;
}

size_t Utf8GetWideStringLengthInUtf8(uint32_t* wideString, size_t count)
{
    size_t i;
    size_t length = 0;

    for (i = 0; i < count; i++) {
        length += Utf8ByteSizeOfCharacterInUtf8(wideString[i]);
    }
    return length;
}

int Utf8ConvertCharacterToUTF32(const char* utf8string, mchar_t* result)
{
    mchar_t character = 0;
    int     byteCount;
    int     index = 0;

    if (!utf8string || !utf8string[0]) {
        return -1;
    }

    byteCount = (unsigned char)g_utf8TrailingBytesTable[(unsigned char)utf8string[0]];
    switch (byteCount) {
        default:
            return -1;
        case 3:
            character += (unsigned char)utf8string[index++];
            character <<= 6;
        case 2:
            character += (unsigned char)utf8string[index++];
            character <<= 6;
        case 1:
            character += (unsigned char)utf8string[index++];
            character <<= 6;
        case 0:
            character += (unsigned char)utf8string[index++];
            character <<= 6;
    }
    character -= g_utf8OffsetTable[byteCount];
    *result = character;
    return 0;
}

int Utf8ConvertCharacterToUtf8(uint32_t character, void* utf8buffer, size_t* length)
{
	char   encodingBuffer[10] = { 0 };
	size_t byteCount = 0;
	int    result = 0;

	if (character <= 0x7F)  /* 0XXX XXXX one byte */
    {
		encodingBuffer[0] = (char)character;
		byteCount = 1;
    }
	else if (character <= 0x7FF)  /* 110X XXXX  two bytes */
    {
		encodingBuffer[0] = (char)(0xC0 | (character >> 6));
		encodingBuffer[1] = (char)(0x80 | (character & 0x3F));
		byteCount = 2;
    }
	else if (character <= 0xFFFF)  /* 1110 XXXX  three bytes */
    {
		encodingBuffer[0] = (char)(0xE0 | (character >> 12));
		encodingBuffer[1] = (char)(0x80 | ((character >> 6) & 0x3F));
		encodingBuffer[2] = (char)(0x80 | (character & 0x3F));
		byteCount = 3;
        
		/* Sanity no special characters */
		if (character == 0xFFFE || character == 0xFFFF)
			result = -1;
    }
	else if (character <= 0x1FFFFF)  /* 1111 0XXX  four bytes */
    {
		encodingBuffer[0] = (char)(0xF0 | (character >> 18));
		encodingBuffer[1] = (char)(0x80 | ((character >> 12) & 0x3F));
		encodingBuffer[2] = (char)(0x80 | ((character >> 6) & 0x3F));
		encodingBuffer[3] = (char)(0x80 | (character & 0x3F));
		byteCount = 4;
        
		if (character > 0x10FFFF)
			result = -1;
    }
	else if (character <= 0x3FFFFFF)  /* 1111 10XX  five bytes */
    {
		encodingBuffer[0] = (char)(0xF8 | (character >> 24));
		encodingBuffer[1] = (char)(0x80 | (character >> 18));
		encodingBuffer[2] = (char)(0x80 | ((character >> 12) & 0x3F));
		encodingBuffer[3] = (char)(0x80 | ((character >> 6) & 0x3F));
		encodingBuffer[4] = (char)(0x80 | (character & 0x3F));
		byteCount = 5;
		result = -1;
    }
	else if (character <= 0x7FFFFFFF)  /* 1111 110X  six bytes */
    {
		encodingBuffer[0] = (char)(0xFC | (character >> 30));
		encodingBuffer[1] = (char)(0x80 | ((character >> 24) & 0x3F));
		encodingBuffer[2] = (char)(0x80 | ((character >> 18) & 0x3F));
		encodingBuffer[3] = (char)(0x80 | ((character >> 12) & 0x3F));
		encodingBuffer[4] = (char)(0x80 | ((character >> 6) & 0x3F));
		encodingBuffer[5] = (char)(0x80 | (character & 0x3F));
		byteCount = 6;
		result = -1;
    }
	else
		result = -1;

	// Write buffer only if it's a valid byte sequence
	if (!result && utf8buffer)
		memcpy(utf8buffer, &encodingBuffer, byteCount);

	*length = byteCount;
    return result;
}

mchar_t Utf8GetNextCharacterInString(const char* string, int* indexp)
{
	mchar_t currentChar = 0;
	int     size = 0;
    int     currentIndex = *indexp;

    if (!string || !*string) {
        return MSTRING_EOS;
    }

	/**
	 * UTF8 is encoded so that the first byte can be anything, but
     * the following bytes in the sequence will be continuation-byte formatted (b & 0xC0 == 80).
	 * That means the first byte will always be valid, and thus we should only check from byte 2.
	 */
	do {
		currentChar <<= 6;
		currentChar += (unsigned char)string[currentIndex++];
		size++;
	} while (string[currentIndex] && IsUTF8(string[currentIndex]));

    // size will never be 0, and thus this is perfectly valid
    currentChar -= g_utf8OffsetTable[size - 1];

	*indexp = currentIndex;
	return currentChar;
}

size_t Utf8CharacterCountInString(const char* string)
{
	size_t length = 0;
	int    index = 0;
    size_t limit;

	if (!string) {
		return 0;
	}

    limit = strlen(string);
	while (index < limit) {
        if (Utf8GetNextCharacterInString(string, &index) == MSTRING_EOS) {
            break;
        }
		length++;
	}
	return length;
}

size_t Utf8ByteCountInString(const char* string)
{
	if (!string) {
		return 0;
	}
	return strlen(string);
}
