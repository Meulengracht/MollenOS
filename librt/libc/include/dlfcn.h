/**
 * Copyright 2019, Philip Meulengracht
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
 */

#ifndef __DLFCN_H__
#define __DLFCN_H__

#define RTLD_LAZY    0
#define RTLD_NOW     0

#define RTLD_GLOBAL  (1 << 1)
#define RTLD_LOCAL   (1 << 2)

#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT    ((void *)-1)

_CODE_BEGIN
CRTDECL(void*, dlopen (const char* file, int mode));
CRTDECL(int,   dlclose(void* handle));
CRTDECL(void*, dlsym(void* restrict handle, const char* restrict name));
CRTDECL(char*, dlerror(void));
_CODE_END

#endif //!__DLFCN_H__
