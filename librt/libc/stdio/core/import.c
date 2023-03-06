/**
 * Copyright 2023, Philip Meulengracht
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

#include <internal/_io.h>
#include "private.h"

static void
StdioInheritObject(
        _In_ struct stdio_handle* inheritHandle)
{
    stdio_handle_t* handle;
    int             status;
    TRACE("[inhert] iod %i, handle %u", inheritHandle->fd, inheritHandle->object.handle);

    status = stdio_handle_create(inheritHandle->fd, inheritHandle->wxflag | WX_INHERITTED, &handle);
    if (!status) {
        if (handle->fd == STDOUT_FILENO) {
            g_stdout._fd = handle->fd;
        }
        else if (handle->fd == STDIN_FILENO) {
            g_stdint._fd = handle->fd;
        }
        else if (handle->fd == STDERR_FILENO) {
            g_stderr._fd = handle->fd;
        }

        stdio_handle_clone(handle, inheritHandle);
        if (handle->ops.inherit(handle) != OS_EOK) {
            TRACE(" > failed to inherit fd %i", inheritHandle->fd);
            stdio_handle_destroy(handle);
        }
    }
    else {
        WARNING(" > failed to create inheritted handle with fd %i", inheritHandle->fd);
    }
}

extern stdio_ops_t g_fmemOps;
extern stdio_ops_t g_memstreamOps;
extern stdio_ops_t g_evtOps;
extern stdio_ops_t g_iosetOps;
extern stdio_ops_t g_fileOps;
extern stdio_ops_t g_pipeOps;
extern stdio_ops_t g_ipcOps;

static stdio_ops_t*
__GetOpsFromSignature(
        _In_ unsigned int signature)
{
    switch (signature) {
        case FMEM_SIGNATURE: return &g_fmemOps;
        case MEMORYSTREAM_SIGNATURE: return &g_memstreamOps;
        case PIPE_SIGNATURE: return &g_pipeOps;
        case FILE_SIGNATURE: return &g_fileOps;
        case IPC_SIGNATURE: return &g_ipcOps;
        case EVENT_SIGNATURE: return &g_evtOps;
        case IOSET_SIGNATURE: return &g_iosetOps;
        case NET_SIGNATURE: return NULL;
        default: {
            assert(0 && "unsupported io-descriptor signature");
        }
    }
    return NULL;
}

size_t
OSHandleDeserialize(
        _In_ struct OSHandle* handle,
        _In_ const void*      buffer);

static size_t
__ParseInheritationHeader(
        _In_ uint8_t* headerData)
{
    struct InheritationHeader* header = (struct InheritationHeader*)&headerData[0];
    int                        status;
    stdio_handle_t*            handle;
    size_t                     bytesParsed = 0;

    // First we create the stdio handle, for this we only need what we can read
    // in the header. The payload will then be parsed, and last, the OS handle
    status = stdio_handle_create2(
            header->IOD,
            header->IOFlags,
            header->XTFlags,
            header->Signature,
            __GetOpsFromSignature(header->Signature),
            NULL,
            &handle
    );
    assert(status == 0);
    bytesParsed += sizeof(struct InheritationHeader);

    // We've parsed the initial data required to set up a new io object.
    // Now we import the OS handle stuff
    bytesParsed += OSHandleDeserialize(&handle->handle, &headerData[bytesParsed]);

    // Handle any implementation specific importation
    if (handle->ops.deserialize) {
        bytesParsed += handle->ops.deserialize(handle, &headerData[bytesParsed]);
    }
    return bytesParsed;
}

static void
__ParseInheritationBlock(
        _In_ void* inheritanceBlock)
{
    struct InheritationBlock* block = inheritanceBlock;
    size_t                    bytesConsumed = 0;

    for (int i = 0; i < block->Count; i++) {
        bytesConsumed += __ParseInheritationHeader(&block->Data[bytesConsumed]);
    }
}

void
CRTReadInheritanceBlock(
        _In_ void* inheritanceBlock)
{
    if (inheritanceBlock != NULL) {
        __ParseInheritationBlock(inheritanceBlock);
    }
}
