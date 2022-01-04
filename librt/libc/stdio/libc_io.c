/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * C Standard Library
 * - Standard IO Support functions
 */

#ifdef LIBC_KERNEL
#include <os/spinlock.h>
#include <threading.h>
#include <threads.h>
#include <stdio.h>

spinlock_t __GlbPrintLock = _SPN_INITIALIZER_NP(spinlock_plain);
FILE __GlbStdout = { 0 }, __GlbStdin = { 0 }, __GlbStderr = { 0 };

OsStatus_t
_lock_stream(
    _In_ FILE *file)
{
    if (!file) {
        return OsInvalidParameters;
    }

    if (!(file->_flag & _IOSTRG)) {
        spinlock_acquire(&__GlbPrintLock);
    }
    return OsSuccess;
}

OsStatus_t
_unlock_stream(
    _In_ FILE *file)
{
    if (!file) {
        return OsInvalidParameters;
    }
    
    if (!(file->_flag & _IOSTRG)) {
        spinlock_release(&__GlbPrintLock);
    }
    return OsSuccess;
}

FILE *
stdio_get_std(
    _In_ int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &__GlbStdout;
        }
        case STDIN_FILENO: {
            return &__GlbStdin;
        }
        case STDERR_FILENO: {
            return &__GlbStderr;
        }
        default: {
            return NULL;
        }
    }
}

int wctomb(char *mbchar, wchar_t wchar)
{
    _CRT_UNUSED(mbchar);
    _CRT_UNUSED(wchar);
    return 0;
}

thrd_t thrd_current(void) {
    return (thrd_t)ThreadCurrentHandle();
}

#else
//#define __TRACE
#include <assert.h>
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <ds/collection.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <internal/_io.h>
#include <io.h>
#include <ctt_input_service.h>
#include <os/keycodes.h>
#include <os/mollenos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Collection_t g_stdioObjects = COLLECTION_INIT(KeyInteger); // TODO hashtable
static FILE         g_stdout = { 0 };
static FILE         g_stdint = { 0 };
static FILE         g_stderr = { 0 };

/**
 * Returns whether or not the handle should be inheritted by sub-processes based on the requested
 * startup information and the handle settings.
 */
static OsStatus_t
StdioIsHandleInheritable(
    _In_ ProcessConfiguration_t* configuration,
    _In_ stdio_handle_t*         handle)
{
    OsStatus_t osSuccess = OsSuccess;

    if (handle->wxflag & WX_DONTINHERIT) {
        osSuccess = OsError;
    }

    // If we didn't request to inherit one of the handles, then we don't account it
    // for being the one requested.
    if (handle->fd == configuration->StdOutHandle &&
        !(configuration->InheritFlags & PROCESS_INHERIT_STDOUT)) {
        osSuccess = OsError;
    }
    else if (handle->fd == configuration->StdInHandle &&
             !(configuration->InheritFlags & PROCESS_INHERIT_STDIN)) {
        osSuccess = OsError;
    }
    else if (handle->fd == configuration->StdErrHandle &&
             !(configuration->InheritFlags & PROCESS_INHERIT_STDERR)) {
        osSuccess = OsError;
    }
    else if (!(configuration->InheritFlags & PROCESS_INHERIT_FILES)) {
        if (handle->fd != configuration->StdOutHandle &&
            handle->fd != configuration->StdInHandle &&
            handle->fd != configuration->StdErrHandle) {
            osSuccess = OsError;
        }
    }

    TRACE("[can_inherit] iod %i, handle %u: %s",
            handle->fd, handle->object.handle,
            (osSuccess == OsSuccess) ? "yes" : "no");
    return osSuccess;
}

static size_t
StdioGetNumberOfInheritableHandles(
    _In_ ProcessConfiguration_t* configuration)
{
    size_t numberOfFiles = 0;
    LOCK_FILES();
    foreach(Node, &g_stdioObjects) {
        stdio_handle_t* object = (stdio_handle_t*)Node->Data;
        if (StdioIsHandleInheritable(configuration, object) == OsSuccess) {
            numberOfFiles++;
        }
    }
    UNLOCK_FILES();
    return numberOfFiles;
}

OsStatus_t
StdioCreateInheritanceBlock(
    _In_  ProcessConfiguration_t* configuration,
    _Out_ void**                  inheritationBlockOut,
    _Out_ size_t*                 inheritationBlockLengthOut)
{
    stdio_inheritation_block_t* inheritationBlock;
    size_t                      numberOfObjects;
    int                         i = 0;

    assert(configuration != NULL);

    if (configuration->InheritFlags == PROCESS_INHERIT_NONE) {
        return OsSuccess;
    }

    numberOfObjects = StdioGetNumberOfInheritableHandles(configuration);
    if (numberOfObjects != 0) {
        size_t inheritationBlockLength;

        inheritationBlockLength = sizeof(stdio_inheritation_block_t) + (numberOfObjects * sizeof(struct stdio_handle));
        inheritationBlock       = (stdio_inheritation_block_t*)malloc(inheritationBlockLength);
        if (!inheritationBlock) {
            return OsOutOfMemory;
        }

        TRACE("[add_inherit] length %u", inheritationBlockLength);
        inheritationBlock->handle_count = numberOfObjects;
        
        LOCK_FILES();
        foreach(Node, &g_stdioObjects) {
            stdio_handle_t* object = (stdio_handle_t*)Node->Data;
            if (StdioIsHandleInheritable(configuration, object) == OsSuccess) {
                memcpy(&inheritationBlock->handles[i], object, sizeof(struct stdio_handle));
                
                // Check for this fd to be equal to one of the custom handles
                // if it is equal, we need to update the fd of the handle to our reserved
                if (object->fd == configuration->StdOutHandle) {
                    inheritationBlock->handles[i].fd = STDOUT_FILENO;
                }
                if (object->fd == configuration->StdInHandle) {
                    inheritationBlock->handles[i].fd = STDIN_FILENO;
                }
                if (object->fd == configuration->StdErrHandle) {
                    inheritationBlock->handles[i].fd = STDERR_FILENO;
                }
                i++;
            }
        }
        UNLOCK_FILES();
        
        *inheritationBlockOut       = (void*)inheritationBlock;
        *inheritationBlockLengthOut = inheritationBlockLength;
    }
    return OsSuccess;
}

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
        if (handle->ops.inherit(handle) != OsSuccess) {
            TRACE(" > failed to inherit fd %i", inheritHandle->fd);
            stdio_handle_destroy(handle, 0);
        }
    }
    else {
        WARNING(" > failed to create inheritted handle with fd %i", inheritHandle->fd);
    }
}

void StdioConfigureStandardHandles(
    _In_ void* inheritanceBlock)
{
    stdio_inheritation_block_t* block = inheritanceBlock;
    stdio_handle_t*             handle_out;
    stdio_handle_t*             handle_in;
    stdio_handle_t*             handle_err;
    int                         i;
    TRACE("[libc] [parse_inherit] 0x%" PRIxIN, block);
    
    // Handle inheritance
    if (block != NULL) {
        for (i = 0; i < block->handle_count; i++) {
            StdioInheritObject(&block->handles[i]);
        }
    }

    // Make sure all default handles have been set for std. The operations for
    // stdout and stderr will be null operations, as no output has been specified
    // for this process. If the process wants to get output it must reopen the
    // stdout/stderr handles.
    handle_out = stdio_handle_get(STDOUT_FILENO);
    if (!handle_out) {
        stdio_handle_create(STDOUT_FILENO, WX_DONTINHERIT, &handle_out);
    }

    handle_in = stdio_handle_get(STDIN_FILENO);
    if (!handle_in) {
        stdio_handle_create(STDIN_FILENO, WX_DONTINHERIT, &handle_in);
    }
    
    handle_err = stdio_handle_get(STDERR_FILENO);
    if (!handle_err) {
        stdio_handle_create(STDERR_FILENO, WX_DONTINHERIT, &handle_err);
    }

    stdio_handle_set_buffered(handle_out, &g_stdout, _IOWRT | _IOLBF); // we buffer stdout as default
    stdio_handle_set_buffered(handle_in, &g_stdint, _IOREAD | _IOLBF); // we also buffer stdint as default
    stdio_handle_set_buffered(handle_err, &g_stderr, _IOWRT | _IONBF);
}

static int __close_io_descriptors(unsigned int excludeFlags)
{
    CollectionItem_t* node;
    stdio_handle_t*   handle;
    int               filesClosed = 0;
    
    LOCK_FILES();
    node = CollectionBegin(&g_stdioObjects);
    while (node) {
        handle = (stdio_handle_t*)node->Data;
        if (excludeFlags && (handle->wxflag & excludeFlags)) {
            node = CollectionNext(node);
            continue;
        }

        // Load next node before closing, as it will be removed
        node = CollectionNext(node);
        
        // Is it a buffered stream or raw?
        if (handle->buffered_stream) {
            fclose(handle->buffered_stream);
        }
        else {
            close(handle->fd);
        }
        filesClosed++;
    }
    UNLOCK_FILES();
    return filesClosed;
}

void StdioInitialize(void)
{
    stdio_bitmap_initialize();
}

_CRTIMP void StdioCleanup(void)
{
    // flush all file buffers and close handles
    io_buffer_flush_all(_IOWRT | _IOREAD);

    // close all handles that are not marked _PRIO, and then lastly
    // close the _PRIO handles
    __close_io_descriptors(WX_PRIORITY);
    __close_io_descriptors(0);
}

int stdio_handle_create(int fd, int flags, stdio_handle_t** handle_out)
{
    stdio_handle_t* handle;
    DataKey_t       key;
    int             updated_fd;

    // the bitmap allocator handles both cases if we want to allocate a specific
    // or just the first free fd
    updated_fd = stdio_bitmap_allocate(fd);
    if (updated_fd == -1) {
        _set_errno(EMFILE);
        return -1;
    }

    handle = (stdio_handle_t*)malloc(sizeof(stdio_handle_t));
    if (!handle) {
        _set_errno(ENOMEM);
        return -1;
    }
    memset(handle, 0, sizeof(stdio_handle_t));
    
    handle->fd            = updated_fd;
    handle->object.handle = UUID_INVALID;
    handle->object.type   = STDIO_HANDLE_INVALID;
    
    handle->wxflag          = WX_OPEN | flags;
    handle->lookahead[0]    = '\n';
    handle->lookahead[1]    = '\n';
    handle->lookahead[2]    = '\n';
    spinlock_init(&handle->lock, spinlock_recursive);
    stdio_get_null_operations(&handle->ops);

    key.Value.Integer = updated_fd;
    CollectionAppend(&g_stdioObjects, CollectionCreateNode(key, handle));
    TRACE("[stdio_handle_create] success %i", updated_fd);
    
    *handle_out = handle;
    return EOK;
}

void stdio_handle_clone(stdio_handle_t* target, stdio_handle_t* source)
{
    if (!target || !source) {
        return;
    }

    // Copy the stdio object data, and then update ops
    memcpy(&target->object, &source->object, sizeof(stdio_object_t));
    stdio_handle_set_ops_type(target, source->object.type);
}

int stdio_handle_set_handle(stdio_handle_t* handle, UUId_t io_handle)
{
    if (!handle) {
        return EBADF;
    }
    handle->object.handle = io_handle;
    return EOK;
}

int stdio_handle_set_ops_type(stdio_handle_t* handle, int type)
{
    if (!handle) {
        return EBADF;
    }
    
    // Get io operations
    handle->object.type = type;
    switch (type) {
        case STDIO_HANDLE_PIPE: {
            stdio_get_pipe_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_FILE: {
            stdio_get_file_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_SOCKET: {
            stdio_get_net_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_IPCONTEXT: {
            stdio_get_ipc_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_SET: {
            stdio_get_set_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_EVENT: {
            stdio_get_evt_operations(&handle->ops);
        } break;
        
        default: {
            stdio_get_null_operations(&handle->ops);
        } break;
    }
    return EOK;
}

int stdio_handle_set_buffered(stdio_handle_t* handle, FILE* stream, unsigned int stream_flags)
{
    if (!handle) {
        return EBADF;
    }
    
    if (!stream) {
        stream = (FILE*)malloc(sizeof(FILE));
        if (!stream) {
            return ENOMEM;
        }
    }
    
    // Reset the stream structure
    stream->_ptr      = stream->_base = NULL;
    stream->_cnt      = 0;
    stream->_fd       = handle->fd;
    stream->_flag     = stream_flags;
    stream->_tmpfname = NULL;
    
    // associate the stream object
    handle->buffered_stream = stream;
    return EOK;
}

int stdio_handle_destroy(stdio_handle_t* handle, int flags)
{
    DataKey_t key;
    
    if (!handle) {
        return EBADF;
    }
    
    key.Value.Integer = handle->fd;
    CollectionRemoveByKey(&g_stdioObjects, key);
    stdio_bitmap_free(handle->fd);
    free(handle);
    return EOK;
}

int stdio_handle_activity(stdio_handle_t* handle , int activity)
{
    OsStatus_t status = handle_post_notification(handle->object.handle, activity);
    if (status != OsSuccess) {
        OsStatusToErrno(status);
        return -1;
    }
    return 0;
}

void stdio_handle_flag(stdio_handle_t* handle, unsigned int flag)
{
    assert(handle != NULL);
    handle->wxflag |= flag;
}

stdio_handle_t* stdio_handle_get(int iod)
{
    DataKey_t Key = { .Value.Integer = iod };
    return (stdio_handle_t*)CollectionGetDataByKey(&g_stdioObjects, Key, 0);
}

FILE* stdio_get_std(int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &g_stdout;
        }
        case STDIN_FILENO: {
            return &g_stdint;
        }
        case STDERR_FILENO: {
            return &g_stderr;
        }
        default: {
            return NULL;
        }
    }
}

int isatty(int fd)
{
    stdio_handle_t* handle = stdio_handle_get(fd);
    if (!handle) {
        return EBADF;
    }
    return (handle->wxflag & WX_TTY) != 0;
}

Collection_t* stdio_get_handles(void)
{
    return &g_stdioObjects;
}

UUId_t GetNativeHandle(int iod)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    if (!handle) {
        return UUID_INVALID;
    }
    return handle->object.handle;
}

#endif
