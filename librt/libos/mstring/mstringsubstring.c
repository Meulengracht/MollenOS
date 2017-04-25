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

/* Substring - build substring from the given mstring
 * starting at Index with the Length. If the length is -1
 * it takes the rest of string */
MString_t *MStringSubString(MString_t *String, int Index, int Length)
{
	/* Variables needed for the
	* string operation */
	MString_t *SubString = NULL;
	size_t DataLength = 0, DataAllocLength = 0;
	char *sPtr = NULL;

	/* Indices for enumeration of 
	 * the relevant data bytes */
	int cIndex = 0, i = 0, lasti = 0, starti = -1;

	/* Sanitize the parameters */
	if (String->Data == NULL
		|| String->Length == 0) {
		return NULL;
	}
	
	/* Santize the index/length given 
	 * to make sure we don't hit bad lengths */
	if (Index > (int)String->Length
		|| ((((Index + Length) > (int)String->Length)) && Length != -1)) {
		return NULL;
	}

	/* Set data pointer to source */
	sPtr = (char*)String->Data;

	/* Count how many bytes we actually need to copy
	 * from the start-index, save start index */
	while (sPtr[i])
	{
		/* Save position so we can calculate
		 * how many bytes we need to copy */
		lasti = i;

		/* Get next character, make sure to check for errors */
		if (Utf8GetNextCharacterInString(sPtr, &i) == MSTRING_EOS) {
			break;
		}

		/* Sanitize that we have entered
		 * the index to record from, and make sure
		 * to record the start index */
		if (cIndex >= Index
			&& (Length == -1 || (cIndex < (Index + Length)))) {
			if (starti == -1) {
				starti = lasti;
			}
			DataLength += (i - lasti);
		}

		/* Increase */
		cIndex++;
	}

	/* Allocate a new instance of mstring */
	SubString = (MString_t*)dsalloc(sizeof(MString_t));

	/* Calculate Length 
	 * add an extra byte for null terminator */
	DataAllocLength = DIVUP((DataLength + 1), MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Set */
	SubString->Data = dsalloc(DataAllocLength);
	SubString->Length = DataLength;
	SubString->MaxLength = DataAllocLength;

	/* Zero it */
	memset(SubString->Data, 0, DataAllocLength);

	/* Now copy data over, we know start index
	 * and also the length of the data to copy */
	memcpy(SubString->Data, 
		((uint8_t*)String->Data + starti), DataLength);

	/* Done! */
	return SubString;
}
