/* MollenOS
 *
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
 *
 * Dynamic library support
 */

#include "os/services/sharedobject.h"
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

void*
dlopen (
    _In_ const char* filepath,
    _In_ int         mode)
{
    // RTLD_LAZY is not supported
    // RTLD_NOW is default behaviour
    _CRT_UNUSED(mode);
    return (void*)SharedObjectLoad(filepath);
}

int
dlclose(
    _In_ void* handle)
{
    // OsOK resolves to 0 luckily
    return (int)SharedObjectUnload((Handle_t)handle);
}

void*
dlsym(
    _In_ void* restrict       handle,
    _In_ const char* restrict name)
{
    return SharedObjectGetFunction((Handle_t)handle, name);
}

char*
dlerror(void)
{
    return strerror(errno);
}

