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

#include <assert.h>
#include <os/osdefs.h>
#include <string.h>
#include <threads.h>

// Arguments use up to 512 * 5 = 2560
// Response use up to 1024 bytes
// This allows for up to 512 bytes of structure data
#define IPC_ARENA_SIZE        4096
#define IPC_MAX_ARGUMENTS     5
#define IPC_UNTYPED_THRESHOLD 512
#define IPC_RESPONSE_MAX_SIZE 1024
#define IPC_RESPONSE_LOCATION (IPC_MAX_ARGUMENTS * IPC_UNTYPED_THRESHOLD)

#define IPC_ARGUMENT_LENGTH_MASK 0x0FFFFFFF
#define IPC_ARGUMENT_MAPPED      0x80000000

#define IPC_SET_TYPED(Message, Index, Value)           IpcSetTypedArgument(Message, Index, (size_t)Value)
#define IPC_SET_UNTYPED_STRING(Message, Index, String) IpcSetUntypedArgument(Message, Index, (void*)String, strlen(String) + 1)

#define __IPC_UNTYPED_MAPPED(Message, Index)           Message->UntypedArguments[Index].Buffer
#define __IPC_UNTYPED_UNMAPPED(Message, Index)         ((void*)(((uint8_t*)Message) + sizeof(IpcMessage_t) + LODWORD(Message->UntypedArguments[Index].Buffer)))

#define IPC_GET_TYPED(Message, Index)                  Message->TypedArguments[Index]
#define IPC_GET_UNTYPED(Message, Index)                ((Message->UntypedArguments[Index].Length & IPC_ARGUMENT_MAPPED) ? __IPC_UNTYPED_MAPPED(Message, Index) : __IPC_UNTYPED_UNMAPPED(Message, Index))
#define IPC_GET_LENGTH(Message, Index)                 (Message->UntypedArguments[Index].Length & IPC_ARGUMENT_LENGTH_MASK)
#define IPC_GET_STRING(Message, Index)                 ((char*)(((uint8_t*)Message) + sizeof(IpcMessage_t) + LODWORD(Message->UntypedArguments[Index].Buffer)))

#define IPC_CAST_AND_DEREF(Pointer, Type)              (Type)*((size_t*)Pointer)

#define IPC_ASYNCHRONOUS      0x00000001
#define IPC_NO_RESPONSE       0x00000002

typedef struct IpcUntypedArgument {
    void*  Buffer;
    size_t Length;
} IpcUntypedArgument_t;

typedef struct IpcMessage {
    size_t               MetaLength;
    thrd_t               Sender;
    size_t               TypedArguments[IPC_MAX_ARGUMENTS];
    IpcUntypedArgument_t UntypedArguments[IPC_MAX_ARGUMENTS];
} IpcMessage_t;

typedef struct IpcArena {
    _Atomic(int) WriteSyncObject;
    _Atomic(int) ReadSyncObject;
    _Atomic(int) ResponseSyncObject;
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
IpcSetUntypedArgument(
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
    _In_  thrd_t        Target,
    _In_  IpcMessage_t* Message, 
    _In_  unsigned int  Flags,
    _In_  int           Timeout,
    _Out_ void**        ResultBuffer));
    
CRTDECL(OsStatus_t,
IpcGetResponse(
    _In_  size_t Timeout,
    _Out_ void** BufferOut));
    
CRTDECL(OsStatus_t,
IpcReply(
    _In_ IpcMessage_t* Message,
    _In_ void*         Buffer,
    _In_ size_t        Length));

CRTDECL(OsStatus_t,
IpcListen(
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut));

CRTDECL(OsStatus_t,
IpcReplyAndListen(
    _In_  IpcMessage_t*  Message,
    _In_  void*          Buffer,
    _In_  size_t         Length,
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut));

#endif //!__OS_IPC_H__
