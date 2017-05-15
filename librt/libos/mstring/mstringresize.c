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

/* Helper for internal functions
 * to automatically resize the buffer 
 * of a string to be able to fit a certain size */
void MStringResize(MString_t *String, size_t Length)
{
	/* Calculate the new byte-count we 
	 * need to encompass with blocks */
	size_t DataLength = DIVUP(Length, MSTRING_BLOCK_SIZE) * MSTRING_BLOCK_SIZE;

	/* Expand and reset buffer */
	void *Data = dsalloc(DataLength);
	memset(Data, 0, DataLength);

	/* Copy old data over */
	memcpy(Data, String->Data, String->Length);

	/* Free the old buffer */
	dsfree(String->Data);

	/* Update string to new buffer */
	String->MaxLength = DataLength;
	String->Data = Data;
}
