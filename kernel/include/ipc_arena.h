/**
 * MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * IP-Communication
 * - Implementation of inter-thread communication. 
 */

#ifndef __VALI_IPC_ARENA_H__
#define __VALI_IPC_ARENA_H__

#include <os/osdefs.h>

// SOURCE-ARENA
// |
// |------ MESSAGE
// |     |
// |     |--- HEADER
// |     |  |
// |     |  |--- LENGTH
// |     |  |--- SOURCE_ARENA
// |     |  |--- SOURCE_ARENA_RESPONSE_OFFSET
// |     |  |--- NOTIFY_METHOD
// |     |  |--- NOTIFY_DATA
// |     |--- PARAMETERS
// 

#define IPC_NOTIFY_METHOD_NONE   0
#define IPC_NOTIFY_METHOD_HANDLE 1 // Completion handle or regular handle
#define IPC_NOTIFY_METHOD_SIGNAL 2 // SIGIPC
#define IPC_NOTIFY_METHOD_THREAD 3 // Thread callback

#define IPC_MAX_ARGUMENTS        5
#define IPC_ARGUMENT_LENGTH_MASK 0x0FFFFFFF
#define IPC_ARGUMENT_MAPPED      0x80000000
#define IPC_GET_LENGTH(Message, Index)                 (Message->UntypedArguments[Index].Length & IPC_ARGUMENT_LENGTH_MASK)

typedef struct IpcResponsePayloadHeader {
    OsStatus_t Status;
} IpcResponsePayloadHeader_t;

typedef struct IpcResponse {
    UUId_t   Arena;
    uint16_t ArenaOffset;
    uint8_t  NotifyMethod;
    struct {
        void* Context;
        union {
            UUId_t    Handle;
            uintptr_t Callback;
        } Notify;
    } NotifyData;
} IpcResponse_t;

typedef struct IpcUntypedArgument {
    void*  Buffer;
    size_t Length;
} IpcUntypedArgument_t;

typedef struct IpcMessage {
    uint16_t             Length;
    UUId_t               Arena;
    size_t               TypedArguments[IPC_MAX_ARGUMENTS];
    IpcUntypedArgument_t UntypedArguments[IPC_MAX_ARGUMENTS];
    IpcResponse_t        Response;
} IpcMessage_t;

/**
 * IpcArenaCreate
 * * 
 */
KERNELAPI OsStatus_t KERNELABI
IpcArenaCreate(
    _In_  int     MessageCount,
    _In_  size_t  MessageSize,
    _Out_ UUId_t* HandleOut,
    _Out_ void**  UserArenaOut);

/**
 * IpcArenaSendMultiple
 * * 
 */
KERNELAPI OsStatus_t KERNELABI
IpcArenaSendMultiple(
    _In_ IpcMessage_t** Messages,
    _In_ int            MessageCount,
    _In_ size_t         Timeout);

/**
 * IpcArenaSendMultipleAsync
 * * 
 */
KERNELAPI OsStatus_t KERNELABI
IpcArenaSendMultipleAsync(
    _In_ IpcMessage_t** Messages,
    _In_ int            MessageCount,
    _In_ size_t         Timeout);

/**
 * IpcArenaRespondMultiple
 * * 
 */
KERNELAPI OsStatus_t KERNELABI
IpcArenaRespondMultiple(
    _In_ IpcMessage_t** Replies,
    _In_ void**         ReplyBuffers,
    _In_ size_t*        ReplyLengths,
    _In_ int            ReplyCount);

/**
 * IpcArenaResponseMultipleAndWait
 * * 
 */
KERNELAPI OsStatus_t KERNELABI
IpcArenaResponseMultipleAndWait(
    _In_ IpcMessage_t** Replies,
    _In_ void**         ReplyBuffers,
    _In_ size_t*        ReplyLengths,
    _In_ int            ReplyCount,
    _In_ UUId_t         ArenaHandle,
    _In_ IpcMessage_t** Messages,
    _In_ int*           MessageCount,
    _In_ size_t         Timeout);

#endif //!__VALI_IPC_ARENA_H__
