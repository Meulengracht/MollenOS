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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * OS Utilities (Protected) Definitions & Structures
 * - This header describes the base utility-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/contracts/video.h>
#include <os/input.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

static const char* MessageHeader = "LIBC";

void
SystemDebug(
	_In_ int         Type,
	_In_ const char* Format, ...)
{
	va_list Args;
	char    Buffer[256];

	memset(&Buffer[0], 0, sizeof(Buffer));
	va_start(Args, Format);
	vsprintf(&Buffer[0], Format, Args);
	va_end(Args);
    Syscall_Debug(Type, MessageHeader, &Buffer[0]);
}

void
MollenOSEndBoot(void)
{
    Syscall_SystemStart();
}

OsStatus_t
WriteSystemInput(
    _In_ SystemInput_t* Input)
{
    assert(Input != NULL);
    return Syscall_InputEvent(Input);
}

OsStatus_t
WriteSystemKey(
    _In_ SystemKey_t* Key)
{
    assert(Key != NULL);
    return Syscall_KeyEvent(Key);
}

OsStatus_t
QueryDisplayInformation(
    _In_ VideoDescriptor_t *Descriptor)
{
    return Syscall_DisplayInformation(Descriptor);
}

void*
CreateDisplayFramebuffer(void)
{
    return Syscall_CreateDisplayFramebuffer();
}
