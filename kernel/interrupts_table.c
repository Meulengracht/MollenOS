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
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#include <arch/io.h>
#include <ddk/interrupt.h>
#include <interrupts.h>
#include <userevent.h>
#include <ds/streambuffer.h>
#include <memory_region.h>

static InterruptFunctionTable_t FastInterruptTable = {0 };

InterruptFunctionTable_t*
GetFastInterruptTable(void)
{
    return &FastInterruptTable;
}

// ReadIoSpace
static size_t
TableFunctionReadIoSpace(
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
static OsStatus_t
TableFunctionWriteIoSpace(
    _In_ DeviceIo_t*    IoSpace,
    _In_ size_t         Offset,
    _In_ size_t         Value,
    _In_ size_t         Length)
{
    if (IoSpace->Type == DeviceIoPortBased) {
        return WriteDirectIo(IoSpace->Type, IoSpace->Access.Port.Base + Offset, Length, Value);
    }
    return OsError;
}

static OsStatus_t
TableFunctionWriteStreambuffer(
        _In_ UUId_t      handle,
        _In_ const void* buffer,
        _In_ size_t      length)
{
    streambuffer_t* stream;

    if (MemoryRegionGetKernelMapping(handle, (void**)&stream) == OsSuccess) {
        streambuffer_stream_out(stream, (void*)buffer, length, STREAMBUFFER_NO_BLOCK);
    }
    return OsSuccess;
}

void
InitializeInterruptTable(void)
{
    FastInterruptTable.ReadIoSpace  = TableFunctionReadIoSpace;
    FastInterruptTable.WriteIoSpace = TableFunctionWriteIoSpace;
    FastInterruptTable.EventSignal  = UserEventSignal;
    FastInterruptTable.WriteStream  = TableFunctionWriteStreambuffer;
}
