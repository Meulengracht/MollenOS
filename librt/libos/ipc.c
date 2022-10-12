/**
 * Copyright 2020, Philip Meulengracht
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
 */

//#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <ds/streambuffer.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <os/ipc.h>

oserr_t
IPCContextCreate(
        _In_  size_t        length,
        _In_  IPCAddress_t* address,
        _Out_ uuid_t*       handleOut,
        _Out_ void**        ipcContextOut)
{
    oserr_t         oserr;
    streambuffer_t* stream;
    uuid_t          handle;

    TRACE("IPCContextCreate(len=%u, addr=0x" PRIxIN ")", length, address);
    
    if (length == 0 || handleOut == NULL || ipcContextOut == NULL) {
        return OsInvalidParameters;
    }

    oserr = Syscall_IpcContextCreate(length, &handle, &stream);
    if (oserr != OsOK) {
        return oserr;
    }
    
    if (address && address->Type == IPC_ADDRESS_PATH) {
        oserr = handle_set_path(handle, address->Data.Path);
        if (oserr != OsOK) {
            handle_destroy(handle);
            return oserr;
        }
    }

    *handleOut = handle;
    *ipcContextOut = stream;
    return OsOK;
}

oserr_t
IPCContextSend(
        _In_ uuid_t        handle,
        _In_ IPCAddress_t* address,
        _In_ const void*   data,
        _In_ unsigned int  length,
        _In_ int           timeout)
{
    IPCMessage_t  msg;
    IPCMessage_t* msgArray = &msg;

    if (!address || !data || !length) {
        _set_errno(EINVAL);
        return -1;
    }

    msg.SenderHandle = handle;
    msg.Address      = address;
    msg.Payload      = data;
    msg.Length       = length;
    return Syscall_IpcContextSend(&msgArray, 1, timeout);
}

oserr_t
IPCContextRecv(
        _In_  void*        ipcContext,
        _In_  void*        buffer,
        _In_  unsigned int length,
        _In_  int          flags,
        _Out_ uuid_t*      fromHandle,
        _Out_ size_t*      bytesReceived)
{
    size_t          bytesAvailable;
    unsigned int    base;
    unsigned int    state;
    streambuffer_t* stream;
    unsigned int    sbOptions = 0;
    uuid_t          sender;

    if (ipcContext == NULL || buffer == NULL || length == 0) {
        return OsInvalidParameters;
    }

    if (flags & IPC_DONTWAIT) {
        sbOptions |= STREAMBUFFER_NO_BLOCK;
    }
    
    stream         = ipcContext;
    bytesAvailable = streambuffer_read_packet_start(stream, sbOptions, &base, &state);
    if (!bytesAvailable) {
        *fromHandle = UUID_INVALID;
        *bytesReceived = 0;
        return OsOK;
    }

    streambuffer_read_packet_data(stream, &sender, sizeof(uuid_t), &state);
    streambuffer_read_packet_data(stream, buffer, MIN(length, bytesAvailable - sizeof(uuid_t)), &state);
    streambuffer_read_packet_end(stream, base, bytesAvailable);

    *fromHandle = sender;
    *bytesReceived = (int)bytesAvailable;
    return OsOK;
}
