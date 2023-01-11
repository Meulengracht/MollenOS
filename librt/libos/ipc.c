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

#define __need_minmax
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <ds/streambuffer.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <os/ipc.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>

struct _IPCContext {
    SHMHandle_t Handle;
};

struct _IPCContext*
__IPCContextNew(
        _In_ SHMHandle_t* handle)
{
    struct _IPCContext* ipcContext;

    ipcContext = malloc(sizeof(struct _IPCContext));
    if (ipcContext == NULL) {
        return NULL;
    }
    memcpy(&ipcContext->Handle, handle, sizeof(SHMHandle_t));

    streambuffer_construct(
            handle->Buffer,
            handle->Length - sizeof(streambuffer_t),
            STREAMBUFFER_GLOBAL | STREAMBUFFER_MULTIPLE_WRITERS
    );
    return ipcContext;
}

oserr_t
__Create(
        _In_  const char*          key,
        _In_  size_t               length,
        _Out_ struct _IPCContext** ipcContextOut)
{
    SHM_t shm = {
            .Key = key,
            .Flags = SHM_IPC,
            .Type = 0,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
            .Size = length
    };
    SHMHandle_t handle;
    oserr_t     oserr;

    oserr = SHMCreate(&shm, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    *ipcContextOut = __IPCContextNew(&handle);
    if (*ipcContextOut == NULL) {
        (void)SHMDetach(&handle);
        return OS_EOOM;
    }
    return OS_EOK;
}

oserr_t
IPCContextCreate(
        _In_  size_t        length,
        _In_  IPCAddress_t* address,
        _Out_ uuid_t*       handleOut,
        _Out_ void**        ipcContextOut)
{
    oserr_t             oserr;
    struct _IPCContext* ipcContext;
    const char*         key;

    TRACE("IPCContextCreate(len=%u, addr=0x" PRIxIN ")", length, address);

    if (length == 0 || handleOut == NULL || ipcContextOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (address && address->Type == IPC_ADDRESS_PATH) {
        key = address->Data.Path;
    }

    oserr = __Create(key, length, &ipcContext);
    if (oserr != OS_EOK) {
        return oserr;
    }

    *handleOut = ipcContext->Handle.ID;
    *ipcContextOut = ipcContext;
    return OS_EOK;
}

oserr_t
IPCContextSend(
        _In_ uuid_t            handle,
        _In_ IPCAddress_t*     address,
        _In_ const void*       data,
        _In_ unsigned int      length,
        _In_ OSTimestamp_t*    deadline,
        _In_ OSAsyncContext_t* asyncContext)
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
    return Syscall_IPCSend(&msgArray, 1, deadline, asyncContext);
}

oserr_t
IPCContextRecv(
        _In_  void*             ipcContext,
        _In_  void*             buffer,
        _In_  unsigned int      length,
        _In_  int               flags,
        _In_  OSAsyncContext_t* asyncContext,
        _Out_ uuid_t*           fromHandle,
        _Out_ size_t*           bytesReceived)
{
    size_t                    bytesAvailable;
    streambuffer_packet_ctx_t packetCtx;
    streambuffer_t*           stream;
    uuid_t                    sender;
    streambuffer_rw_options_t rwOptions = {
            .flags = 0,
            .async_context = asyncContext,
            .deadline = NULL
    };
    TRACE("IPCContextRecv(async=%i, flags=0x%x)", asyncContext != NULL ? 1 : 0, flags);

    if (ipcContext == NULL || buffer == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    if (flags & IPC_DONTWAIT) {
        rwOptions.flags |= STREAMBUFFER_NO_BLOCK;
    }
    
    stream         = ipcContext;
    bytesAvailable = streambuffer_read_packet_start(stream, &rwOptions, &packetCtx);
    TRACE("IPCContextRecv bytes=%u", (uint32_t)bytesAvailable);
    if (!bytesAvailable) {
        *fromHandle = UUID_INVALID;
        *bytesReceived = 0;
        return OS_EOK;
    }

    streambuffer_read_packet_data(&sender, sizeof(uuid_t), &packetCtx);
    streambuffer_read_packet_data(buffer, MIN(length, bytesAvailable - sizeof(uuid_t)), &packetCtx);
    streambuffer_read_packet_end(&packetCtx);

    *fromHandle = sender;
    *bytesReceived = bytesAvailable;
    return OS_EOK;
}
