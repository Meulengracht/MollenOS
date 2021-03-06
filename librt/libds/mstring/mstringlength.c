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

size_t
MStringLength(
    _In_ MString_t* string)
{
    if (!string || !string->Data || !string->Length) {
        return 0;
    }
	return Utf8CharacterCountInString((const char*)string->Data);
}

size_t
MStringSize(
    _In_ MString_t* string)
{
    if (!string || !string->Data || !string->Length) {
        return 0;
    }
	return string->Length;
}
