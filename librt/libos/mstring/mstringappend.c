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

/* Append MString to MString 
 * This appends the given String
 * the destination string */
void MStringAppendString(MString_t *Destination, MString_t *String)
{
	/* Luckily this is UTF8 */
	size_t NewLength = Destination->Length + String->Length;
	uint8_t *BufPtr = NULL;

	/* Sanitize if destination has 
	 * enough space for both buffers */
	if (NewLength >= Destination->MaxLength) {
		MStringResize(Destination, NewLength);
	}

	/* Cast the pointer */
	BufPtr = (uint8_t*)Destination->Data;

	/* Copy */
	memcpy(BufPtr + Destination->Length, String->Data, String->Length);

	/* Set a null terminator */
	BufPtr[NewLength] = '\0';

	/* Update Length */
	Destination->Length = NewLength;
}

/* Append Character to a given string 
 * the character is assumed to be either 
 * ASCII, UTF16 or UTF32 and NOT utf8 */
void MStringAppendCharacter(MString_t *String, mchar_t Character)
{
	/* Variables needed for addition */
	size_t NewLength = String->Length + Utf8ByteSizeOfCharacterInUtf8(Character);
	uint8_t *BufPtr = NULL;
	size_t cLen = 0;
	int Itr = 0;

	/* Sanitize the length of our storage */ 
	if (NewLength >= String->MaxLength) {
		MStringResize(String, NewLength);
	}

	/* Cast */
	BufPtr = (uint8_t*)String->Data;
	Itr = String->Length;

	/* Append */
	Utf8ConvertCharacterToUtf8(Character, (void*)&BufPtr[Itr], &cLen);

	/* Null-terminate */
	BufPtr[Itr + cLen] = '\0';

	/* Done? */
	String->Length += cLen;
}

/* Appends raw string data to a 
 * given mstring, you must indicate what format
 * the raw string is of so it converts correctly. */
void MStringAppendCharacters(MString_t *String, const char *Characters, MStringType_t DataType)
{
	/* Proxy the data by 
	 * converting it to a mstring */
	MString_t *Proxy = MStringCreate((void*)Characters, DataType);

	/* Now we can easily append them */
	MStringAppendString(String, Proxy);

	/* Cleanup */
	MStringDestroy(Proxy);
}
