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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS System Interface
 */

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <ddk/contracts/video.h>
#include <os/mollenos.h>
#include <os/process.h>
#include <ddk/utils.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *__SysTypeMessage = "LIBC";

void
SystemDebug(
	_In_ int         Type,
	_In_ const char* Format, ...)
{
	va_list Args;
	char    TmpBuffer[256];

	memset(&TmpBuffer[0], 0, sizeof(TmpBuffer));
	va_start(Args, Format);
	vsprintf(&TmpBuffer[0], Format, Args);
	va_end(Args);
    Syscall_Debug(Type, __SysTypeMessage, &TmpBuffer[0]);
}

void
MollenOSEndBoot(void)
{
    Syscall_SystemStart();
}

OsStatus_t
SystemQuery(
	_In_ SystemDescriptor_t* Descriptor)
{
	// Sanitize parameters
	if (Descriptor == NULL) {
		return OsError;
	}
	return Syscall_SystemQuery(Descriptor);
}

OsStatus_t
QueryDisplayInformation(VideoDescriptor_t *Descriptor)
{
    return Syscall_DisplayInformation(Descriptor);
}

void*
CreateDisplayFramebuffer(void)
{
    return Syscall_CreateDisplayFramebuffer();
}

OsStatus_t
GetSystemTime(
	_In_ SystemTime_t* Time)
{
    return Syscall_SystemTime(Time);
}

OsStatus_t
GetSystemTick(
    _In_ int              TickBase,
    _In_ LargeUInteger_t* Tick)
{
    if (TickBase == TIME_PROCESS && !IsProcessModule()) {
        return ProcessGetTickBase((clock_t*)&Tick->QuadPart);
    }
    return Syscall_SystemTick(TickBase, Tick);
}

OsStatus_t
QueryPerformanceFrequency(
	_Out_ LargeInteger_t *Frequency)
{
    return Syscall_SystemPerformanceFrequency(Frequency);
}

OsStatus_t
QueryPerformanceTimer(
	_Out_ LargeInteger_t *Value)
{
    return Syscall_SystemPerformanceTime(Value);
}

OsStatus_t
FlushHardwareCache(
    _In_     int    Cache,
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length)
{
    return Syscall_FlushHardwareCache(Cache, Start, Length);
}
