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

/* Helper 
 * Converts ASCII to UTF-8 
 * returns 0 on success, otherwise error */
int MStringConvertASCIIToUtf8(MString_t *Storage, const char *Source)
{
	/* Variables and make sure to reset length */
	char *dPtr = NULL;
	char *cPtr = (char*)Source;
	size_t DataLength = 0;
	Storage->Length = 0;

	/* We have to count manually, 
	 * one of them might be two bytes */
	while (*cPtr) {
		Storage->Length += Utf8ByteSizeOfCharacterInUtf8((mchar_t)*cPtr);
		cPtr++;
	}

	/* Calculate Length 
	 * include extra byte for terminator */
	DataLength = DIVUP((Storage->Length + 1), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Allocate a buffer */
	Storage->Data = (void*)dsalloc(DataLength);
	Storage->MaxLength = DataLength;

	/* Memset it */
	memset(Storage->Data, 0, DataLength);

	/* Conversion time! */
	dPtr = Storage->Data;
	cPtr = (char*)Source;
	while (*cPtr) {
		size_t Bytes = 0;

		/* Make sure the character is encodable */
		if (!Utf8ConvertCharacterToUtf8((mchar_t)*cPtr, dPtr, &Bytes)) {
			dPtr += Bytes;
		}

		/* Next source item */
		cPtr++;
	}

	/* Done! */
	return 0;
}

/* Helper
 * Converts Latin1 to UTF-8
 * returns 0 on success, otherwise error */
int MStringConvertLatin1ToUtf8(MString_t *Storage, const char *Source)
{
	/* Calculate the string length 
	 * given by the forumale len*2+1 */
	size_t TempLength = strlen(Source) * 2 + 1;
	char *SourcePtr = (char*)Source;
	char *DestPtr = NULL;

	/* Calculate Length */
	size_t DataLength = DIVUP(TempLength, MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Allocate a buffer */
	Storage->Data = (void*)dsalloc(DataLength);
	Storage->MaxLength = DataLength;

	/* Memset it */
	memset(Storage->Data, 0, DataLength);

	/* Iterate the data given and convert */
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

	/* Null terminate */
	*DestPtr = '\0';

	/* Update the actual length of the string */
	Storage->Length = strlen((const char*)Storage->Data);

	/* Done! */
	return 0;
}

/* Helper
 * Converts UTF-16 to UTF-8
 * returns 0 on success, otherwise error */
int MStringConvertUtf16ToUtf8(MString_t *Storage, const char *Source)
{
	/* Variables and make sure to reset length */
	uint16_t *sPtr = (uint16_t*)Source;
	char *dPtr = NULL;
	size_t DataLength = 0;
	Storage->Length = 0;
	
	/* Iterate and count how many bytes
	 * are neccessary for the string */
	while (*sPtr) {
		Storage->Length += Utf8ByteSizeOfCharacterInUtf8((mchar_t)*sPtr);
		sPtr++;
	}

	/* Calculate Length 
	 * but here include terminator bytes */
	DataLength = DIVUP((Storage->Length + 2), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Allocate a buffer */
	Storage->Data = (void*)dsalloc(DataLength);
	Storage->MaxLength = DataLength;

	/* Memset it */
	memset(Storage->Data, 0, DataLength);

	/* Conversion time! */
	sPtr = (uint16_t*)Source;
	dPtr = (char*)Storage->Data;
	while (*sPtr) {
		size_t Bytes = 0;

		/* Make sure the character is encodable */
		if (!Utf8ConvertCharacterToUtf8((mchar_t)*sPtr, dPtr, &Bytes)) {
			dPtr += Bytes;
		}

		/* Advance to next character in string */
		sPtr++;
	}

	/* Done! */
	return 0;
}

/* Helper
 * Converts UTF-32 to UTF-8
 * returns 0 on success, otherwise error */
int MStringConvertUtf32ToUtf8(MString_t *Storage, const char *Source)
{
	/* Variables and make sure to reset length */
	uint32_t *sPtr = (uint32_t*)Source;
	char *dPtr = NULL;
	size_t DataLength = 0;
	Storage->Length = 0;
	
	/* Iterate and count how many bytes
	 * are neccessary for the string */
	while (*sPtr) {
		Storage->Length += Utf8ByteSizeOfCharacterInUtf8((mchar_t)*sPtr);
		sPtr++;
	}

	/* Calculate Length 
	 * but here include terminator bytes */
	DataLength = DIVUP((Storage->Length + 4), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Allocate a buffer */
	Storage->Data = (void*)dsalloc(DataLength);
	Storage->MaxLength = DataLength;

	/* Memset it */
	memset(Storage->Data, 0, DataLength);

	/* Conversion time! */
	sPtr = (uint32_t*)Source;
	dPtr = (char*)Storage->Data;
	while (*sPtr) {
		size_t Bytes = 0;

		/* Make sure the character is encodable */
		if (!Utf8ConvertCharacterToUtf8((mchar_t)*sPtr, dPtr, &Bytes)) {
			dPtr += Bytes;
		}

		/* Advance to next character in string */
		sPtr++;
	}

	/* Done! */
	return 0;
}

/* Helper 
 * Copies UTF-8 to MString UTF-8 
 * returns 0 on success, otherwise error */
int MStringCopyUtf8ToUtf8(MString_t *Storage, const char *Source)
{
	/* Variables */
	size_t DataLength = 0;

	/* Get length */
	Storage->Length = strlen(Source);

	/* Calculate Length 
	 * include extra byte for null-terminator */
	DataLength = DIVUP((Storage->Length + 1), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Allocate a buffer */
	Storage->Data = (void*)dsalloc(DataLength);
	Storage->MaxLength = DataLength;

	/* Memset it */
	memset(Storage->Data, 0, DataLength);

	/* Copy string */
	memcpy(Storage->Data, (const void*)Source, Storage->Length);

	/* Done! */
	return 0;
}

/* Helper 
 * Resets a MString instance to a null string */
void MStringNull(MString_t *Storage)
{
	/* Allocate an empty string */
	if (Storage->Data == NULL) {
		Storage->Data = dsalloc(MSTRING_BLOCK_SIZE);
		Storage->MaxLength = MSTRING_BLOCK_SIZE;
	}
	
	/* Reset memory */
	memset(Storage->Data, 0, Storage->MaxLength);

	/* Reset length */
	Storage->Length = 0;
}

/* Creates a MString instace from string data
 * The possible string-data types are ASCII, UTF8, UTF16, UTF32
 * and it automatically converts the data to an UTf8 representation
 * and keeps it as UTF8 internally */
MString_t *MStringCreate(void *Data, MStringType_t DataType)
{
	/* Variables needed for 
	 * the creation of mstring */
	MString_t *String = NULL;

	/* Allocate a new instance of MString 
	 * So there is always returned a new string */
	String = (MString_t*)dsalloc(sizeof(MString_t));
	String->Data = NULL;

	/* SPECIAL Case: Empty string */
	if (Data == NULL) {
		MStringNull(String);
		return String;
	}

	/* Sanitize the datatype 
	 * we have to handle each type differently */
	if (DataType == StrASCII) {
		if (MStringConvertASCIIToUtf8(String, (const char*)Data)) {
			MStringNull(String);
		}
	}
	else if (DataType == StrUTF8) {
		if (MStringCopyUtf8ToUtf8(String, (const char*)Data)) {
			MStringNull(String);
		}
	}
	else if (DataType == Latin1) {
		if (MStringConvertLatin1ToUtf8(String, (const char*)Data)) {
			MStringNull(String);
		}
	}
	else if (DataType == StrUTF16) {
		if (MStringConvertUtf16ToUtf8(String, (const char*)Data)) {
			MStringNull(String);
		}
	}
	else if (DataType == StrUTF32) {
		if (MStringConvertUtf32ToUtf8(String, (const char*)Data)) {
			MStringNull(String);
		}
	}
	else {
		MStringNull(String);
	}

	/* Done */
	return String;
}
