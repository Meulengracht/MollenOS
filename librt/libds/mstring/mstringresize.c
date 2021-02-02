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

void MStringResize(
        _In_ MString_t* string,
        _In_ size_t     size)
{
    void*  data;
	size_t dataLength;

	// add one to size to account for null terminator
    dataLength = DIVUP(size + 1, MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;
	if (dataLength <= string->MaxLength) {
	    return;
	}

	data = dsalloc(dataLength);
	if (!data) {
	    return;
	}

	memset(data, 0, dataLength);
	memcpy(data, string->Data, string->Length);
	dsfree(string->Data);

    string->MaxLength = dataLength;
    string->Data      = data;
}
