/**
 * MollenOS
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
 * InterProcess Communication Interface
 */

#ifndef __OS_IPC_H__
#define __OS_IPC_H__

#include <os/osdefs.h>

#define IPC_MAX_ARGUMENTS 5

typedef struct {
    void*  Buffer;
    size_t Length;
} IpcUntypedArgument_t;

typedef struct {
    size_t               TypedArguments[IPC_MAX_ARGUMENTS];
    IpcUntypedArgument_t UntypedArguments[IPC_MAX_ARGUMENTS];
} IpcMessage_t;

typedef struct {
    _Atomic(int) WriteSyncObject;
    _Atomic(int) ReadSyncObject;
    UUId_t       SenderHandle;
    IpcMessage_t Message;
    uint8_t      Buffer[1];
} IpcArena_t;

static inline void
IpcInitialize(
    _In_ IpcMessage_t* Message)
{
    memset(Message, 0, sizeof(IpcMessage_t));
}

static inline void
IpcSetTypedArgument(
    _In_ IpcMessage_t* Message,
    _In_ int           Index,
    _In_ size_t        Value)
{
    assert(Index < IPC_MAX_ARGUMENTS);
    Message->TypedArguments[Index] = Value;
}

static inline void
IpcSetUntypedArgumnet(
    _In_ IpcMessage_t* Message,
    _In_ int           Index,
    _In_ void*         Buffer,
    _In_ size_t        Length)
{
    assert(Index < IPC_MAX_ARGUMENTS);
    Message->UntypedArguments[Index].Buffer = Buffer;
    Message->UntypedArguments[Index].Length = Length;
}

CRTDECL(OsStatus_t,
IpcInvoke(
    _In_ thrd_t        Target,
    _In_ IpcMessage_t* Message, 
    _In_ unsigned int  Flags,
    _In_ int           Timeout));
    
CRTDECL(OsStatus_t,
IpcGetResponse(
    _In_  size_t Timeout,
    _Out_ void** BufferOut));
    
CRTDECL(OsStatus_t,
IpcReply());

CRTDECL(OsStatus_t,
IpcListen(
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut));

#endif //!__OS_IPC_H__
