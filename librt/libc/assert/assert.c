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
* MollenOS C-Library - Assert
*/

#include <assert.h>

#ifdef LIBC_KERNEL
#define __MODULE "LIBK"
#include <debug.h>
void _assert_panic(const char* str) {
	DebugPanic(FATAL_SCOPE_KERNEL, NULL, __MODULE, str);
}
#else
#include <ddk/utils.h>
#include <stdlib.h>

void _assert_panic(const char* str)
{
	ERROR(str);
	abort();
}

#endif