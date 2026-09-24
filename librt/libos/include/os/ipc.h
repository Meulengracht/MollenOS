/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __OS_IPCCONTEXT_H__
#define	__OS_IPCCONTEXT_H__

#include <os/types/ipc.h>
#include <os/types/handle.h>
#include <os/types/async.h>
#include <os/types/time.h>

_CODE_BEGIN

/**
 * @brief Creates an IPC context for exchanging messages.
 * @param length Size of the context buffer.
 * @param address IPC address associated with the context.
 * @param handleOut Receives the context handle.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
IPCContextCreate(
        _In_  size_t        length,
        _In_  IPCAddress_t* address,
        _Out_ OSHandle_t*   handleOut));

/**
 * @brief Sends data through an IPC context.
 * @param handle IPC context handle.
 * @param address Destination IPC address.
 * @param data Message data.
 * @param length Message length in bytes.
 * @param deadline Absolute time at which the send expires.
 * @param asyncContext Optional asynchronous operation context.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
IPCContextSend(
        _In_ OSHandle_t*       handle,
        _In_ IPCAddress_t*     address,
        _In_ const void*       data,
        _In_ unsigned int      length,
        _In_ OSTimestamp_t*    deadline,
        _In_ OSAsyncContext_t* asyncContext));

/**
 * @brief Receives data from an IPC context.
 * @param handle IPC context handle.
 * @param buffer Buffer that receives the message.
 * @param length Capacity of buffer in bytes.
 * @param flags Receive behavior flags.
 * @param asyncContext Optional asynchronous operation context.
 * @param fromHandle Receives the sender handle identifier.
 * @param bytesReceived Receives the number of bytes copied.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
IPCContextRecv(
        _In_  OSHandle_t*       handle,
        _In_  void*             buffer,
        _In_  unsigned int      length,
        _In_  int               flags,
        _In_  OSAsyncContext_t* asyncContext,
        _Out_ uuid_t*           fromHandle,
        _Out_ size_t*           bytesReceived));

_CODE_END
#endif //!__OS_IPCCONTEXT_H__
