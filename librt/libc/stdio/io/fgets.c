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
* MollenOS C Library - File Get String
*/

#include <internal/_io.h>
#include <stdio.h>

char *fgets(
	_In_ char *s, 
	_In_ int size, 
	_In_ FILE *file)
{
	int cc = EOF;
	char *buf_start = s;

	_lock_stream(file);
	while ((size > 1) && (cc = fgetc(file)) != EOF && cc != '\n') {
		*s++ = (char)cc;
		size--;
	}
	if ((cc == EOF) && (s == buf_start)) { // If nothing read, return 0
		_unlock_stream(file);
		return NULL;
	}
	if ((cc != EOF) && (size > 1))
		*s++ = cc;
	*s = '\0';
	
	_unlock_stream(file);
	return buf_start;
}
