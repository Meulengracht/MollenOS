/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
	if (Length == -1) {
	    // ensure large enough buffer
        MStringResize(Destination, Source->MaxLength);

		memcpy(Destination->Data, Source->Data, Source->Length);
		Destination->Length = Source->Length;
	}
	else {
		/* Calculate byte length to copy */
		char*    srcData = (char*)Source->Data;
        uint8_t* destData;
		int Count = Length, Index = 0;

		while (srcData[Index] && Count) {
			mchar_t NextCharacter = Utf8GetNextCharacterInString(srcData, &Index);
			if (NextCharacter == MSTRING_EOS) {
				break;
			}
			Count--;
		}

		// ensure space enough for the copy
        MStringResize(Destination, Index);

		// copy the data over and then we null terminate
		memcpy(Destination->Data, Source->Data, Index);

		destData = (uint8_t*)Destination->Data;
        destData[Index] = '\0';
	}
}
