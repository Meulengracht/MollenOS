/**
 * Copyright 2023, Philip Meulengracht
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

#include <internal/_syscalls.h>
#include <ddk/video.h>
#include <ddk/utils.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

void
SystemDebug(
        _In_ enum OSSysLogLevel Level,
        _In_ const char*        Format, ...)
{
	va_list args;
	char    buffer[256];

	va_start(args, Format);
	vsnprintf(&buffer[0], sizeof(buffer) - 1, Format, args);
	va_end(args);

    Syscall_Debug(Level, &buffer[0]);
}

oserr_t
QueryBootVideoInformation(
        _In_ OSBootVideoDescriptor_t* Descriptor)
{
    size_t bytesQueried;
    return OSSystemQuery(
            OSSYSTEMQUERY_BOOTVIDEOINFO,
            Descriptor,
            sizeof(OSBootVideoDescriptor_t),
            &bytesQueried
    );
}

void* CreateDisplayFramebuffer(void)
{
    void*   framebuffer;
    oserr_t oserr = Syscall_MapBootFramebuffer(&framebuffer);
    if (oserr != OS_EOK) {
        return NULL;
    }
    return framebuffer;
}

oserr_t
DdkUtilsMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* bufferLengthOut)
{
    if (!bufferOut || !bufferLengthOut) {
        return OS_EINVALPARAMS;
    }
    return Syscall_MapRamdisk(bufferOut, bufferLengthOut);
}
