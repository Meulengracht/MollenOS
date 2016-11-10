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

/* Get's the number of characters in a mstring
 * and not the actual byte length. */
size_t MStringLength(MString_t *String)
{
	/* Sanity */
	if (String->Data == NULL
		|| String->Length == 0)
		return 0;

	/* Use our Utf8 helper */
	return Utf8CharacterCountInString((const char*)String->Data);
}

/* Retrieves the number of bytes used 
 * in the given mstring */
size_t MStringSize(MString_t *String)
{
	/* Sanity */
	if (String == NULL
		|| String->Data == NULL) {
		return 0;
	}
	else {
		return String->Length;
	}
}
