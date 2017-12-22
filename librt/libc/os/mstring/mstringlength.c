/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS String Library
 *  - Contains implementation of a generic string library that can
 *    hold data in an UTF8 maner. It can convert all typical string formats
 *    to UTF-8.
 */

/* Includes 
 * - System */
#include "mstringprivate.h"

/* MStringLength
 * Get's the number of characters in a mstring and not the actual byte length. */
size_t
MStringLength(
    _In_ MString_t *String)
{
	// Sanitize input
	if (String->Data == NULL || String->Length == 0) {
        return 0;
    }
	return Utf8CharacterCountInString((const char*)String->Data);
}

/* MStringSize
 * Retrieves the number of bytes used in the given mstring */
size_t
MStringSize(
    _In_ MString_t *String)
{
	// Sanitize input
	if (String->Data == NULL || String->Length == 0) {
        return 0;
    }
	return String->Length;
}
