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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic String Library
 *    - Managed string library for manipulating of strings in a managed format and to support
 *      conversions from different formats to UTF-8
 */

#include "mstringprivate.h"
#include <assert.h>

static int
MStringConvertASCIIToUtf8(
    _In_ MString_t*  Storage,
    _In_ const char* Source)
{
    char *dPtr = NULL;
    char *cPtr = (char*)Source;
    size_t DataLength = 0;

    // Get the length of the data
    Storage->Length = 0;
    while (*cPtr) {
        Storage->Length += Utf8ByteSizeOfCharacterInUtf8((mchar_t)*cPtr);
        cPtr++;
    }

    DataLength = DIVUP((Storage->Length + 1), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;
    Storage->Data = (void*)dsalloc(DataLength);
    Storage->MaxLength = DataLength;
    memset(Storage->Data, 0, DataLength);

    dPtr = Storage->Data;
    cPtr = (char*)Source;
    while (*cPtr) {
        size_t Bytes = 0;

        if (!Utf8ConvertCharacterToUtf8((mchar_t)*cPtr, dPtr, &Bytes)) {
            dPtr += Bytes;
        }
        cPtr++;
    }
    return 0;
}

static int
MStringConvertLatin1ToUtf8(
    _In_ MString_t*  Storage,
    _In_ const char* Source)
{
    size_t DataLength;
    size_t TempLength;
    char*  SourcePtr;
    char*  DestPtr;
    assert(Source != NULL);

    TempLength         = strlen(Source) * 2 + 1;
    SourcePtr          = (char*)Source;
    DataLength         = DIVUP(TempLength, MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;
    Storage->MaxLength = DataLength;

    Storage->Data = (void*)dsalloc(DataLength);
    memset(Storage->Data, 0, DataLength);
    
    DestPtr = (char*)Storage->Data;
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

    *DestPtr = '\0';
    Storage->Length = strlen((const char*)Storage->Data);
    return 0;
}

static int
MStringConvertUtf16ToUtf8(
    _In_ MString_t * Storage,
    _In_ const char* Source)
{
    uint16_t *sPtr = (uint16_t*)Source;
    char *dPtr = NULL;
    size_t DataLength = 0;

    // Get length of data
    Storage->Length = 0;
    while (*sPtr) {
        Storage->Length += Utf8ByteSizeOfCharacterInUtf8((mchar_t)*sPtr);
        sPtr++;
    }

    DataLength = DIVUP((Storage->Length + 2), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;
    Storage->Data = (void*)dsalloc(DataLength);
    Storage->MaxLength = DataLength;
    memset(Storage->Data, 0, DataLength);

    sPtr = (uint16_t*)Source;
    dPtr = (char*)Storage->Data;
    while (*sPtr) {
        size_t Bytes = 0;

        if (!Utf8ConvertCharacterToUtf8((mchar_t)*sPtr, dPtr, &Bytes)) {
            dPtr += Bytes;
        }
        sPtr++;
    }
    return 0;
}

static int
MStringConvertUtf32ToUtf8(
    _In_ MString_t*  Storage,
    _In_ const char* Source)
{
    uint32_t *sPtr = (uint32_t*)Source;
    char *dPtr = NULL;
    size_t DataLength;
    
    // Get length of data
    Storage->Length = 0;
    while (*sPtr) {
        Storage->Length += Utf8ByteSizeOfCharacterInUtf8((mchar_t)*sPtr);
        sPtr++;
    }

    DataLength = DIVUP((Storage->Length + 4), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;
    Storage->Data = (void*)dsalloc(DataLength);
    Storage->MaxLength = DataLength;
    memset(Storage->Data, 0, DataLength);

    sPtr = (uint32_t*)Source;
    dPtr = (char*)Storage->Data;
    while (*sPtr) {
        size_t Bytes = 0;

        if (!Utf8ConvertCharacterToUtf8((mchar_t)*sPtr, dPtr, &Bytes)) {
            dPtr += Bytes;
        }
        sPtr++;
    }
    return 0;
}

static int
MStringCopyUtf8ToUtf8(
    _In_ MString_t*  Storage,
    _In_ const char* Source)
{
    size_t DataLength;
    assert(Source != NULL);

    Storage->Length = strlen(Source);
    DataLength      = DIVUP((Storage->Length + 1), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

    Storage->Data       = (void*)dsalloc(DataLength);
    Storage->MaxLength  = DataLength;

    memset(Storage->Data, 0, DataLength);
    memcpy(Storage->Data, (const void*)Source, Storage->Length);
    return 0;
}

static void
MStringNull(
    _In_ MString_t* Storage)
{
    if (Storage->Data == NULL) {
        Storage->Data      = dsalloc(MSTRING_BLOCK_SIZE);
        Storage->MaxLength = MSTRING_BLOCK_SIZE;
    }
    memset(Storage->Data, 0, Storage->MaxLength);
    Storage->Length = 0;
}

void
MStringReset(
    _In_ MString_t*    String,
    _In_ const char*   NewString,
    _In_ MStringType_t DataType)
{
    assert(String != NULL);

    if (String->Data != NULL) {
        dsfree(String->Data);
        String->Data = NULL;
    }
    
    if (NewString == NULL) {
        MStringNull(String);
        return;
    }

    if (DataType == StrASCII) {
        if (MStringConvertASCIIToUtf8(String, NewString)) {
            MStringNull(String);
        }
    }
    else if (DataType == StrUTF8) {
        if (MStringCopyUtf8ToUtf8(String, NewString)) {
            MStringNull(String);
        }
    }
    else if (DataType == Latin1) {
        if (MStringConvertLatin1ToUtf8(String, NewString)) {
            MStringNull(String);
        }
    }
    else if (DataType == StrUTF16) {
        if (MStringConvertUtf16ToUtf8(String, NewString)) {
            MStringNull(String);
        }
    }
    else if (DataType == StrUTF32) {
        if (MStringConvertUtf32ToUtf8(String, NewString)) {
            MStringNull(String);
        }
    }
    else {
        MStringNull(String);
    }
}

MString_t*
MStringCreate(
    _In_ const char*   Data,
    _In_ MStringType_t DataType)
{
    MString_t* string = (MString_t*)dsalloc(sizeof(MString_t));
    if (!string) {
        return NULL;
    }

    memset((void*)string, 0, sizeof(MString_t));
    MStringReset(string, Data, DataType);
    return string;
}

MString_t*
MStringClone(
    _In_ MString_t* String)
{
    MString_t* Clone;

    assert(String != NULL);
    if (String->Length != 0 && String->Data != NULL) {
        Clone = (MString_t*)dsalloc(sizeof(MString_t));
        memset((void*)Clone, 0, sizeof(MString_t));
        MStringCopyUtf8ToUtf8(Clone, MStringRaw(String));
    }
    else {
        Clone = MStringCreate(NULL, StrUTF8);
    }
    return Clone;
}

void
MStringZero(
    _In_ MString_t* String)
{
    assert(String != NULL);
    MStringNull(String);
}
