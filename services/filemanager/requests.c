/**
 * Copyright 2021, Philip Meulengracht
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
 * Virtual File Request Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <gracht/server.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/requests.h>

#include "sys_file_service_server.h"

extern void OpenFile(FileSystemRequest_t* request, void*);
extern void CloseFile(FileSystemRequest_t* request, void*);
extern void DeletePath(FileSystemRequest_t* request, void*);
extern void ReadFile(FileSystemRequest_t* request, void*);
extern void WriteFile(FileSystemRequest_t* request, void*);
extern void Seek(FileSystemRequest_t* request, void*);
extern void ReadFileAbsolute(FileSystemRequest_t* request, void*);
extern void WriteFileAbsolute(FileSystemRequest_t* request, void*);
extern void Flush(FileSystemRequest_t* request, void*);
extern void Move(FileSystemRequest_t* request, void*);
extern void Link(FileSystemRequest_t* request, void*);
extern void GetPosition(FileSystemRequest_t* request, void*);
extern void GetOptions(FileSystemRequest_t* request, void*);
extern void SetOptions(FileSystemRequest_t* request, void*);
extern void GetSize(FileSystemRequest_t* request, void*);
extern void SetSize(FileSystemRequest_t* request, void*);
extern void StatFromHandle(FileSystemRequest_t* request, void*);
extern void StatFromPath(FileSystemRequest_t* request, void*);
extern void StatLinkPathFromPath(FileSystemRequest_t* request, void*);
extern void GetFullPath(FileSystemRequest_t* request, void*);

static _Atomic(UUId_t) g_requestId = ATOMIC_VAR_INIT(1);

static FileSystemRequest_t*
CreateRequest(struct gracht_message* message, UUId_t processId)
{
    FileSystemRequest_t* request;

    request = malloc(sizeof(FileSystemRequest_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!request) {
        ERROR("CreateRequest out of memory for message allocation!");
        return NULL;
    }

    request->id = atomic_fetch_add(&g_requestId, 1);
    request->state = FileSystemRequest_CREATED;
    request->processId = processId;
    gracht_server_defer_message(message, &request->message[0]);
    usched_mtx_init(&request->lock);
    usched_cnd_init(&request->signal);
    return request;
}

void VfsRequestDestroy(FileSystemRequest_t* request)
{
    assert(request != NULL);

    free(request);
}

void VfsRequestSetState(FileSystemRequest_t* request, enum FileSystemRequestState state)
{
    assert(request != NULL);

    request->state = state;
}

void sys_file_open_invocation(struct gracht_message* message,
                              const UUId_t processId, const char* path,
                              const unsigned int options, const unsigned int access)
{
    FileSystemRequest_t* request;

    TRACE("svc_file_open_callback()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_open_response(message, OsOutOfMemory, UUID_INVALID);
        return;
    }

    // perform initial input verification
    if (!path) {
        sys_file_open_response(request->message, OsInvalidParameters, UUID_INVALID);
        return;
    }

    request->parameters.open.path = strdup(path);
    request->parameters.open.access = access;
    request->parameters.open.options = options;
    usched_task_queue((usched_task_fn)OpenFile, request);
}

void sys_file_close_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_close_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_close_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.close.fileHandle = handle;
    usched_task_queue((usched_task_fn)CloseFile, request);
}

void sys_file_delete_invocation(struct gracht_message* message,
                                const UUId_t processId, const char* path, const unsigned int flags)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_delete_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_delete_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.delete_path.path = strdup(path);
    request->parameters.delete_path.options = flags;
    usched_task_queue((usched_task_fn)DeletePath, request);
}

void sys_file_transfer_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle,
                                  const enum sys_transfer_direction direction, const UUId_t bufferHandle,
                                  const size_t offset, const size_t length)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_transfer_invocation()");
    if (bufferHandle == UUID_INVALID || length == 0) {
        sys_file_transfer_response(message, OsInvalidParameters, 0);
        return;
    }

    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_transfer_response(message, OsOutOfMemory, 0);
        return;
    }

    // initialize parameters
    request->parameters.transfer.fileHandle = handle;
    request->parameters.transfer.bufferHandle = bufferHandle;
    request->parameters.transfer.length = length;
    request->parameters.transfer.offset = offset;

    if (direction == SYS_TRANSFER_DIRECTION_READ) {
        usched_task_queue((usched_task_fn)ReadFile, request);
    }
    else {
        usched_task_queue((usched_task_fn)WriteFile, request);
    }
}

void sys_file_seek_invocation(struct gracht_message* message, const UUId_t processId,
                              const UUId_t handle, const unsigned int seekLow, const unsigned int seekHigh)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_seek_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_seek_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.seek.fileHandle = handle;
    request->parameters.seek.position_low = seekLow;
    request->parameters.seek.position_high = seekHigh;
    usched_task_queue((usched_task_fn)Seek, request);
}

void sys_file_transfer_absolute_invocation(struct gracht_message* message, const UUId_t processId,
                                           const UUId_t handle, const enum sys_transfer_direction direction,
                                           const unsigned int seekLow, const unsigned int seekHigh,
                                           const UUId_t bufferHandle, const size_t offset, const size_t length)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_transfer_absolute_invocation()");
    if (bufferHandle == UUID_INVALID || length == 0) {
        sys_file_transfer_absolute_response(message, OsInvalidParameters, 0);
        return;
    }

    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_transfer_absolute_response(message, OsOutOfMemory, 0);
        return;
    }

    // initialize parameters
    request->parameters.transfer_absolute.fileHandle = handle;
    request->parameters.transfer_absolute.position_low = seekLow;
    request->parameters.transfer_absolute.position_high = seekHigh;
    request->parameters.transfer_absolute.bufferHandle = bufferHandle;
    request->parameters.transfer_absolute.length = length;
    request->parameters.transfer_absolute.offset = offset;

    if (direction == SYS_TRANSFER_DIRECTION_READ) {
        usched_task_queue((usched_task_fn)ReadFileAbsolute, request);
    }
    else {
        usched_task_queue((usched_task_fn)WriteFileAbsolute, request);
    }
}

void sys_file_flush_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_flush_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_flush_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.flush.fileHandle = handle;
    usched_task_queue((usched_task_fn)Flush, request);
}

void sys_file_move_invocation(struct gracht_message* message, const UUId_t processId,
                              const char* source, const char* destination, const uint8_t copy)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_move_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_move_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.move.from = strdup(source);
    request->parameters.move.to = strdup(destination);
    request->parameters.move.copy = copy;
    usched_task_queue((usched_task_fn)Move, request);
}

void sys_file_link_invocation(struct gracht_message* message, const UUId_t processId,
                              const char* source, const char* destination, const uint8_t symbolic)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_link_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_link_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.link.from = strdup(source);
    request->parameters.link.to = strdup(destination);
    request->parameters.link.symbolic = symbolic;
    usched_task_queue((usched_task_fn)Link, request);
}

void sys_file_get_position_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_get_position_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_get_position_response(message, OsOutOfMemory, 0, 0);
        return;
    }

    request->parameters.get_position.fileHandle = handle;
    usched_task_queue((usched_task_fn)GetPosition, request);
}

void sys_file_get_options_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_get_options_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_get_options_response(message, OsOutOfMemory, 0, 0);
        return;
    }

    request->parameters.get_options.fileHandle = handle;
    usched_task_queue((usched_task_fn)GetOptions, request);
}

void sys_file_set_options_invocation(struct gracht_message* message, const UUId_t processId,
                                     const UUId_t handle, const unsigned int options, const unsigned int access)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_set_options_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_set_options_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.set_options.fileHandle = handle;
    request->parameters.set_options.options = options;
    request->parameters.set_options.access = access;
    usched_task_queue((usched_task_fn)SetOptions, request);
}

void sys_file_get_size_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_get_size_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_get_size_response(message, OsOutOfMemory, 0, 0);
        return;
    }

    request->parameters.get_size.fileHandle = handle;
    usched_task_queue((usched_task_fn)GetSize, request);
}

void sys_file_set_size_invocation(struct gracht_message* message, const UUId_t processId,
                                  const UUId_t handle, const unsigned int sizeLow, const unsigned int sizeHigh)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_set_size_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_set_size_response(message, OsOutOfMemory);
        return;
    }

    request->parameters.set_size.fileHandle = handle;
    request->parameters.set_size.size_low = sizeLow;
    request->parameters.set_size.size_high = sizeHigh;
    usched_task_queue((usched_task_fn)SetSize, request);
}

void sys_file_get_path_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_get_path_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        char zero[1] = { 0 };
        sys_file_get_path_response(message, OsOutOfMemory, &zero[0]);
        return;
    }

    request->parameters.stat_handle.fileHandle = handle;
    usched_task_queue((usched_task_fn)GetFullPath, request);
}

void sys_file_fstat_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    struct sys_file_descriptor gdescriptor = { 0 };
    FileSystemRequest_t* request;

    TRACE("sys_file_fstat_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_fstat_response(message, OsOutOfMemory, &gdescriptor);
        return;
    }

    request->parameters.stat_handle.fileHandle = handle;
    usched_task_queue((usched_task_fn)StatFromHandle, request);
}

void sys_file_fstat_path_invocation(struct gracht_message* message, const UUId_t processId, const char* path)
{
    struct sys_file_descriptor gdescriptor = { 0 };
    FileSystemRequest_t* request;

    TRACE("sys_file_fstat_path_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        sys_file_fstat_path_response(message, OsOutOfMemory, &gdescriptor);
        return;
    }

    request->parameters.stat_path.path = strdup(path);
    usched_task_queue((usched_task_fn)StatFromPath, request);
}

void sys_file_fstat_link_invocation(struct gracht_message* message, const UUId_t processId, const char* path)
{
    FileSystemRequest_t* request;

    TRACE("sys_file_fstat_link_invocation()");
    request = CreateRequest(message, processId);
    if (!request) {
        char zero[1] = { 0 };
        sys_file_fstat_link_response(message, OsOutOfMemory, &zero[0]);
        return;
    }

    request->parameters.stat_path.path = strdup(path);
    usched_task_queue((usched_task_fn)StatLinkPathFromPath, request);
}

void sys_file_fsstat_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    struct sys_filesystem_descriptor gdescriptor = { 0 };
    TRACE("sys_file_fsstat_invocation()");

    // @todo not implemented
    sys_file_fsstat_response(message, OsNotSupported, &gdescriptor);
}

void sys_file_fsstat_path_invocation(struct gracht_message* message, const UUId_t processId, const char* path)
{
    struct sys_filesystem_descriptor gdescriptor = { 0 };
    TRACE("sys_file_fsstat_path_invocation()");

    // @todo not implemented
    sys_file_fsstat_path_response(message, OsNotSupported, &gdescriptor);
}
