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

void
MStringCopy(
    _In_ MString_t* Destination,
    _In_ MString_t* Source,
    _In_ int        DestinationIndex,
    _In_ int        SourceIndex,
    _In_ int        Length)
{
	if (Destination == NULL || Source == NULL || Source->Length == 0) {
        return;
    }

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
