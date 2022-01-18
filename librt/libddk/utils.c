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
 *
 * OS Utilities (Protected) Definitions & Structures
 * - This header describes the base utility-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/video.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

void
SystemDebug(
	_In_ int         Type,
	_In_ const char* Format, ...)
{
	va_list args;
	char    buffer[256];

	va_start(args, Format);
	vsnprintf(&buffer[0], sizeof(buffer) - 1, Format, args);
	va_end(args);

    Syscall_Debug(Type, &buffer[0]);
}

void
MollenOSEndBoot(void)
{
    Syscall_SystemStart();
}

OsStatus_t
QueryDisplayInformation(VideoDescriptor_t* Descriptor)
{
    return Syscall_DisplayInformation(Descriptor);
}

void* CreateDisplayFramebuffer(void)
{
    void*      framebuffer;
    OsStatus_t osStatus = Syscall_MapBootFramebuffer(&framebuffer);
    if (osStatus != OsSuccess) {
        return NULL;
    }
    return framebuffer;
}

OsStatus_t
DdkUtilsMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* bufferLengthOut)
{
    if (!bufferOut || !bufferLengthOut) {
        return OsInvalidParameters;
    }
    return Syscall_MapRamdisk(bufferOut, bufferLengthOut);
}
