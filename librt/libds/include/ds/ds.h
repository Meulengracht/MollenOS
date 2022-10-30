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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic Data Structures
 */

#ifndef __DATASTRUCTURES__
#define __DATASTRUCTURES__

#include <ds/dsdefs.h>

typedef struct FutexParameters OSFutexParameters_t;

DSDECL(void*, dsalloc(size_t size));
DSDECL(void,  dsfree(void* pointer));

#ifdef __TRACE
DSDECL(void, dstrace(const char* fmt, ...));
#else
#define dstrace(...)
#endif
DSDECL(void, dswarning(const char* fmt, ...));
DSDECL(void, dserror(const char* fmt, ...));

DSDECL(void, dswait(FutexParameters_t*));
DSDECL(void, dswake(FutexParameters_t*));

#endif //!__DATASTRUCTURES__
