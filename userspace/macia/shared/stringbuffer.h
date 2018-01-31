/* The Macia Language (MACIA)
*
* Copyright 2016, Philip Meulengracht
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
* Macia - Stringbuffer (Shared)
*/

#ifndef _STRINGBUFFER_H_
#define _STRINGBUFFER_H_

/* Includes
 * - Library */
#include <cstddef>

/* The structure for the
 * stringbuffer, includes functions */
typedef struct _StringBuffer {

	/* Number of characters appended */
	size_t Count;

	/* Length of Characters */
	size_t Capacity;

	/* Array of char * pointers added w/ append() */
	char *Characters;

	/* Functions */
	void(*Append) (struct _StringBuffer *Sb, char c);
	const char * (*ToString) (struct _StringBuffer *Sb);
	void(*Dispose) (struct _StringBuffer **Sb);

} StringBuffer_t;

/* Only quasi-public function 
 * remainder wrapped in StringBuffer struct members */
StringBuffer_t *GetStringBuffer();

#endif