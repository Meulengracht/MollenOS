/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include "../common/private.h"
#include <string.h>

#define IS_UTF8(c) (((c) & 0xC0) == 0x80)

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

mchar_t mstr_tochar(const char* u8)
{
    mchar_t character = 0;
    int     byteCount;
    int     index = 0;

    if (!u8 || !u8[0]) {
        return 0;
    }

    byteCount = (unsigned char)g_utf8TrailingBytesTable[(unsigned char)u8[0]];
    switch (byteCount) {
        default:
            return 0;
        case 3:
            character += (unsigned char)u8[index++];
            character <<= 6;
        case 2:
            character += (unsigned char)u8[index++];
            character <<= 6;
        case 1:
            character += (unsigned char)u8[index++];
            character <<= 6;
        case 0:
            character += (unsigned char)u8[index++];
            character <<= 6;
    }
    character -= g_utf8OffsetTable[byteCount];
    return character;
}

mchar_t mstr_next(const char* u8, int* indexp)
{
    mchar_t currentChar  = 0;
    int     size         = 0;
    int     currentIndex = *indexp;

    if (u8 == NULL || !*u8) {
        return MSTRING_EOS;
    }

    /**
     * UTF8 is encoded so that the first byte can be anything, but
     * the following bytes in the sequence will be continuation-byte formatted (b & 0xC0 == 80).
     * That means the first byte will always be valid, and thus we should only check from byte 2.
     */
    do {
        currentChar <<= 6;
        currentChar += (unsigned char)u8[currentIndex++];
        size++;
    } while (u8[currentIndex] && IS_UTF8(u8[currentIndex]));

    // size will never be 0, and thus this is perfectly valid
    currentChar -= g_utf8OffsetTable[size - 1];

    *indexp = currentIndex;
    return currentChar;
}

size_t mstr_len_u8(const char* u8)
{
    size_t length = 0;
    int    u8i    = 0;

    if (u8 == NULL) {
        return 0;
    }

    while (u8[u8i]) {
        mstr_next(u8, &u8i);
        length++;
    }
    return length;
}

void mstr_u8_to_internal(const char* u8, mchar_t* out) {
    int u8i      = 0;
    int outIndex = 0;

    if (u8 == NULL || out == NULL) {
        return;
    }

    while (u8[u8i]) {
        out[outIndex++] = mstr_next(u8, &u8i);
    }
}

int mstr_fromchar(mchar_t character, char* u8, size_t* length)
{
    char   encodingBuffer[10] = { 0 };
    size_t byteCount          = 0;
    int    result             = 0;

    if (character <= 0x7F) {
        /* 0XXX XXXX one byte */
        encodingBuffer[0] = (char)character;
        byteCount = 1;
    } else if (character <= 0x7FF)  {
        /* 110X XXXX  two bytes */
        encodingBuffer[0] = (char)(0xC0 | (character >> 6));
        encodingBuffer[1] = (char)(0x80 | (character & 0x3F));
        byteCount = 2;
    } else if (character <= 0xFFFF) {
        /* 1110 XXXX  three bytes */
        encodingBuffer[0] = (char)(0xE0 | (character >> 12));
        encodingBuffer[1] = (char)(0x80 | ((character >> 6) & 0x3F));
        encodingBuffer[2] = (char)(0x80 | (character & 0x3F));
        byteCount = 3;

        /* Sanity no special characters */
        if (character == 0xFFFE || character == 0xFFFF) {
            result = -1;
        }
    } else if (character <= 0x1FFFFF) {
        /* 1111 0XXX  four bytes */
        encodingBuffer[0] = (char)(0xF0 | (character >> 18));
        encodingBuffer[1] = (char)(0x80 | ((character >> 12) & 0x3F));
        encodingBuffer[2] = (char)(0x80 | ((character >> 6) & 0x3F));
        encodingBuffer[3] = (char)(0x80 | (character & 0x3F));
        byteCount = 4;

        if (character > 0x10FFFF) {
            result = -1;
        }
    } else if (character <= 0x3FFFFFF) {
        /* 1111 10XX  five bytes */
        encodingBuffer[0] = (char)(0xF8 | (character >> 24));
        encodingBuffer[1] = (char)(0x80 | (character >> 18));
        encodingBuffer[2] = (char)(0x80 | ((character >> 12) & 0x3F));
        encodingBuffer[3] = (char)(0x80 | ((character >> 6) & 0x3F));
        encodingBuffer[4] = (char)(0x80 | (character & 0x3F));
        byteCount = 5;
        result = -1;
    }
    else if (character <= 0x7FFFFFFF) {
        /* 1111 110X  six bytes */
        encodingBuffer[0] = (char)(0xFC | (character >> 30));
        encodingBuffer[1] = (char)(0x80 | ((character >> 24) & 0x3F));
        encodingBuffer[2] = (char)(0x80 | ((character >> 18) & 0x3F));
        encodingBuffer[3] = (char)(0x80 | ((character >> 12) & 0x3F));
        encodingBuffer[4] = (char)(0x80 | ((character >> 6) & 0x3F));
        encodingBuffer[5] = (char)(0x80 | (character & 0x3F));
        byteCount = 6;
        result = -1;
    } else {
        result = -1;
    }

    // Write buffer only if it's a valid byte sequence
    if (!result && u8) {
        memcpy(u8, &encodingBuffer, byteCount);
    }
    *length = byteCount;
    return result;
}

char* mstr_u8(mstring_t* string)
{
    char   *u8, *p;
    size_t length = 0;
    size_t i;

    // get length of string in u8 format
    for (i = 0; i < string->__length; i++) {
        size_t byteCount;
        if (mstr_fromchar(string->__data[i], NULL, &byteCount)) {
            // invalid charater, skip conversion of this
            continue;
        }
        length += byteCount;
    }

    // C-strings must be zero terminated, so allocate an extra byte for that
    // and ensure it is 0. This way we also handle zero strings just fine.
    u8 = stralloc(length + 1);
    if (u8 == NULL) {
        return NULL;
    }

    // Now we have enough space for the conversion, so we can perform the actual
    // conversion.
    for (i = 0, p = u8; i < string->__length; i++) {
        size_t byteCount;
        if (mstr_fromchar(string->__data[i], p, &byteCount)) {
            // invalid charater, skip conversion of this
            continue;
        }
        p += byteCount;
    }

    // place the zero terminator
    *p = '\0';
    return u8;
}

static inline int __is_surrogate(short uc) {
    return (uc - 0xd800u) < 2048u;
}

static inline int __is_high_surrogate(short uc) {
    return (uc & 0xfffffc00) == 0xd800;
}

static inline int __is_low_surrogate(short uc) {
    return (uc & 0xfffffc00) == 0xdc00;
}

static mchar_t __surrogate_to_utf32(short high, short low) {
    return (high << 10) + low - 0x35fdc00;
}

size_t mstr_len_u16(const short* u16)
{
    size_t length = 0;
    int    index  = 0;
    while (u16[index]) {
        if (!__is_surrogate(u16[index])) {
            length++;
        } else {
            // Is there enough characters to convert?
            if (__is_high_surrogate(u16[index]) && u16[index + 1] && __is_low_surrogate(u16[index + 1])) {
                length++;
            }
            index++;
        }
        index++;
    }
    return length;
}

void mstr_u16_to_internal(const short* u16, mchar_t* out)
{
    size_t outIndex = 0;
    int    index    = 0;
    while (u16[index]) {
        if (!__is_surrogate(u16[index])) {
            out[outIndex++] = u16[index];
        } else {
            // Is there enough characters to convert?
            if (__is_high_surrogate(u16[index]) && u16[index + 1] && __is_low_surrogate(u16[index + 1])) {
                out[outIndex++] = __surrogate_to_utf32(u16[index], u16[index + 1]);
            }
            index++;
        }
        index++;
    }
}
