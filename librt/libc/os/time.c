/**
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
 * MollenOS System Interface
 */

#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/process.h>

OsStatus_t
VaGetWallClock(
        _In_ LargeInteger_t* time)
{
    if (!time) {
        return OsInvalidParameters;
    }
    return Syscall_ReadWallClock(time);
}

OsStatus_t
VaGetClockTick(
        _In_ enum VaClockSourceType source,
        _In_ LargeUInteger_t*       tickOut)
{
    if (!tickOut) {
        return OsInvalidParameters;
    }

    if (source == VaClockSourceType_PROCESS && !__crt_is_phoenix()) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        OsStatus_t               osStatus;
        
        sys_process_get_tick_base(GetGrachtClient(), &msg.base, ProcessGetCurrentId());
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_get_tick_base_result(GetGrachtClient(), &msg.base, &osStatus,
                                         &tickOut->u.LowPart, &tickOut->u.HighPart);
        return osStatus;
    }
    return Syscall_ClockTick(source, tickOut);
}

OsStatus_t
VaGetClockFrequency(
        _In_ enum VaClockSourceType source,
        _In_ LargeUInteger_t*       frequencyOut)
{
    if (!frequencyOut) {
        return OsInvalidParameters;
    }

    // The frequency is a bit more funny, because all ticks inheritly source the same frequency
    // so the source, unless HPC is requested, is relatively #dontcare.
    return Syscall_ClockFrequency(source, frequencyOut);
}

OsStatus_t
VaSleep(
        _In_      LargeUInteger_t* duration,
        _Out_Opt_ LargeUInteger_t* remaining)
{
    if (!duration || !duration->QuadPart) {
        return OsInvalidParameters;
    }

    return Syscall_Sleep(duration, remaining);
}

OsStatus_t
VaStall(
        _In_ LargeUInteger_t* duration)
{
    if (!duration || !duration->QuadPart) {
        return OsInvalidParameters;
    }

    return Syscall_Stall(duration);
}
