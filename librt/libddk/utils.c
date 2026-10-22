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
#include <ddk/capabilities.h>
#include <ddk/video.h>
#include <ddk/utils.h>
#include <os/osdefs.h>
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

oserr_t
CapabilityGrant(
    _In_ Capabilities_t capabilities,
    _In_ uuid_t         memorySpaceHandle)
{
    return Syscall_CapabilityGrant(capabilities, memorySpaceHandle);
}

static struct {
    const char*    name;
    Capabilities_t capability;
} g_capabilityStrings[] = {
    { "grant-control",           CAPABILITY_GRANT_CAPABILITY },
    { "firmware-access",         CAPABILITY_FIRMWARE_ACCESS },
    { "ramdisk-access",          CAPABILITY_RAMDISK_ACCESS },
    { "deviceio-access",         CAPABILITY_DEVICEIO_ACCESS },
    { "deviceio-management",     CAPABILITY_DEVICEIO_MANAGEMENT },
    { "interrupt-control",       CAPABILITY_INTERRUPT_CONTROL },
    { "memoryspace-management",  CAPABILITY_MEMORYSPACE_MANAGEMENT },
};

Capabilities_t
CapabilityParse(
    _In_ const char* capabilityString)
{
    if (capabilityString == NULL) {
        return 0;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(g_capabilityStrings); i++) {
        if (strcmp(g_capabilityStrings[i].name, capabilityString) == 0) {
            return g_capabilityStrings[i].capability;
        }
    }
    return 0; // Invalid capability string
}
