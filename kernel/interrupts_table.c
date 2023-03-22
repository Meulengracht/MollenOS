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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#include <arch/io.h>
#include <ddk/interrupt.h>
#include <debug.h>
#include <ds/streambuffer.h>
#include <interrupts.h>
#include <userevent.h>
#include <shm.h>
#include <stdarg.h>
#include <stdio.h>

static InterruptFunctionTable_t FastInterruptTable = {0 };

InterruptFunctionTable_t*
GetFastInterruptTable(void)
{
    return &FastInterruptTable;
}

// ReadIoSpace
static size_t
__FunctionReadIoSpace(
    _In_ DeviceIo_t* IoSpace,
    _In_ size_t      Offset,
    _In_ size_t      Length)
{
    size_t Value = 0;
    if (IoSpace->Type == DeviceIoPortBased) {
        ReadDirectIo(IoSpace->Type, IoSpace->Access.Port.Base + Offset, Length, &Value);
    }
    return Value;
}

// WriteIoSpace
static oserr_t
__FunctionWriteIoSpace(
    _In_ DeviceIo_t*    IoSpace,
    _In_ size_t         Offset,
    _In_ size_t         Value,
    _In_ size_t         Length)
{
    if (IoSpace->Type == DeviceIoPortBased) {
        return WriteDirectIo(IoSpace->Type, IoSpace->Access.Port.Base + Offset, Length, Value);
    }
    return OS_EUNKNOWN;
}

static void
__FunctionTrace(
        _In_ const char* format, ...)
{
    char    buffer[128];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(&buffer[0], sizeof(buffer) - 1, format, arguments);
    va_end(arguments);

    LogAppendMessage(OSSYSLOGLEVEL_TRACE, &buffer[0]);
}

void
InitializeInterruptTable(void)
{
    FastInterruptTable.ReadIoSpace  = __FunctionReadIoSpace;
    FastInterruptTable.WriteIoSpace = __FunctionWriteIoSpace;
    FastInterruptTable.EventSignal  = UserEventSignal;
    FastInterruptTable.Trace        = __FunctionTrace;
}
