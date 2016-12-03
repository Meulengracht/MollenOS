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
		if (Source->Length >= Destination->MaxLength) {
			MStringResize(Destination, Source->MaxLength);
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
		while (DataPtr[Index] && Count) {
			mchar_t NextCharacter = Utf8GetNextCharacterInString(DataPtr, &Index);
			if (NextCharacter == MSTRING_EOS) {
				break;
			}
			Count--;
		}

		/* Is destination large enough? */
		if ((size_t)Index >= Destination->MaxLength) {
			MStringResize(Destination, Index);
		}

		/* Copy */
		memcpy(Destination->Data, Source->Data, Index);

		/* Null Terminate */
		uint8_t *NullPtr = (uint8_t*)Destination->Data;
		NullPtr[Index] = '\0';
	}
}
