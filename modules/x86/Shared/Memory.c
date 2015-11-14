/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Module Shared Library ( Memory Functions )
*/

/* Includes */
#include <crtdefs.h>
#include <Module.h>

/* Typedefs */
typedef void *(*kMemAlloc)(size_t Size);
typedef void (*kMemFree)(void *Ptr);

/* Heap Operations */
void *kmalloc(size_t sz)
{
	return ((kMemAlloc)GlbFunctionTable[kFuncMemAlloc])(sz);
}

void kfree(void *p)
{
	((kMemFree)GlbFunctionTable[kFuncMemAlloc])(p);
}