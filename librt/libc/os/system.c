/**
 * MollenOS
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

#include <internal/_ipc.h>
#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/process.h>

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
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        OsStatus_t               status;
        
        svc_process_get_tick_base(GetGrachtClient(), &msg.base, ProcessGetCurrentId());
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
        svc_process_get_tick_base_result(GetGrachtClient(), &msg.base, &status,
            &Tick->u.LowPart, &Tick->u.HighPart);
        return status;
    }
    return Syscall_SystemTick(TickBase, Tick);
}

OsStatus_t
QueryPerformanceFrequency(
	_In_ LargeInteger_t* Frequency)
{
    return Syscall_SystemPerformanceFrequency(Frequency);
}

OsStatus_t
QueryPerformanceTimer(
	_In_ LargeInteger_t* Value)
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
