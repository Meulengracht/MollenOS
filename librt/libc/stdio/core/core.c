/**
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
 */

#include "internal/_file.h"

//#define __TRACE
#include <assert.h>
#include <os/notification_queue.h>
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <errno.h>
#include <internal/_io.h>
#include <io.h>
#include "private.h"

// hashtable functions for g_stdioObjects
static uint64_t stdio_hash(const void* element);
static int      stdio_cmp(const void* element1, const void* element2);

static hashtable_t g_stdioObjects = { 0 };
static FILE        g_stdout       = { 0 };
static FILE        g_stdint       = { 0 };
static FILE        g_stderr       = { 0 };

hashtable_t* IODescriptors(void)
{
    return &g_stdioObjects;
}

void
StdioConfigureStandardHandles(
        _In_ void* inheritanceBlock)
{
    stdio_handle_t*           handle_out;
    stdio_handle_t*           handle_in;
    stdio_handle_t*           handle_err;
    int                       i;
    TRACE("[libc] [parse_inherit] 0x%" PRIxIN, block);

    // Before ensuring the state of the std* FILE descriptors, we must
    // parse the inheritance block as it can affect their state.
    CRTReadInheritanceBlock(inheritanceBlock);

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

    // pre-initialize some data members which are not reset by
    // stdio_handle_set_buffered.
    usched_mtx_init(&g_stdout._lock, USCHED_MUTEX_RECURSIVE);
    usched_mtx_init(&g_stdint._lock, USCHED_MUTEX_RECURSIVE);
    usched_mtx_init(&g_stderr._lock, USCHED_MUTEX_RECURSIVE);

    stdio_handle_set_buffered(handle_out, &g_stdout, _IOWRT | _IOLBF); // we buffer stdout as default
    stdio_handle_set_buffered(handle_in, &g_stdint, _IOREAD | _IOLBF); // we also buffer stdint as default
    stdio_handle_set_buffered(handle_err, &g_stderr, _IOWRT | _IONBF);
}

struct __iod_close_context {
    unsigned int excludeFlags;
    int          filesClosed;
};

static void
__close_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    struct stdio_object_entry*  entry = (struct stdio_object_entry*)element;
    stdio_handle_t*             handle = entry->handle;
    struct __iod_close_context* context = userContext;
    _CRT_UNUSED(index);

    if (handle == NULL) {
        // already cleaned up
        return;
    }

    if (context->excludeFlags && (handle->XTFlags & context->excludeFlags)) {
        return;
    }

    // Is it a buffered stream or raw?
    if (handle->Stream) {
        fclose(handle->Stream);
    } else {
        close(handle->IOD);
    }

    // Cleanup handle, and mark the handle NULL to not cleanup twice.
    // We do a *VERY* crude cleanup here as we are called on shutdown
    // which means we don't really care about proper cleanup
    entry->handle = NULL;

    context->filesClosed++;
}

static int
__close_io_descriptors(
        _In_ unsigned int excludeFlags)
{
    struct __iod_close_context context = {
            .excludeFlags = excludeFlags,
            .filesClosed  = 0
    };
    
    LOCK_FILES();
    hashtable_enumerate(
            &g_stdioObjects,
            __close_entry,
            &context
    );
    UNLOCK_FILES();
    return context.filesClosed;
}

int stdio_handle_create2(
        _In_  int              iod,
        _In_  int              ioFlags,
        _In_  int              wxFlags,
        _In_  unsigned int     signature,
        _In_  stdio_ops_t*     ops,
        _In_  void*            opsCtx,
        _Out_ stdio_handle_t** handleOut)
{
    stdio_handle_t* handle;
    int             updated_fd;

    // the bitmap allocator handles both cases if we want to allocate a specific
    // or just the first free fd
    updated_fd = stdio_bitmap_allocate(iod);
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
    
    handle->IOD            = updated_fd;
    handle->XTFlags       = WX_OPEN | flags;
    handle->Peek[0] = '\n';
    handle->Peek[1] = '\n';
    handle->Peek[2] = '\n';


    hashtable_set(&g_stdioObjects, &(struct stdio_object_entry) {
        .id = updated_fd,
        .handle = handle
    });
    TRACE("[stdio_handle_create] success %i", updated_fd);
    
    *handle_out = handle;
    return EOK;
}

void stdio_handle_clone(
        _In_  stdio_handle_t*  handle,
        _Out_ stdio_handle_t** handleOut)
{
    if (!target || !source) {
        return;
    }

    // Copy the stdio object data, and then update ops
    memcpy(&target->object, &source->object, sizeof(stdio_object_t));
    stdio_handle_set_ops_type(target, source->object.type);
}

int stdio_handle_set_handle(
        _In_ stdio_handle_t* handle,
        _In_ OSHandle_t*     osHandle)
{
    if (!handle) {
        return EBADF;
    }
    memcpy(&handle->OSHandle, osHandle, sizeof(OSHandle_t));
    return EOK;
}

int stdio_handle_set_ops_ctx(stdio_handle_t* handle, void* ctx)
{
    if (!handle) {
        return EBADF;
    }
    handle->OpsContext = ctx;
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
        memset(stream, 0, sizeof(FILE));

        // TODO move to construct function
        usched_mtx_init(&stream->_lock, USCHED_MUTEX_RECURSIVE);
    }
    
    // Reset the stream structure
    stream->_ptr      = stream->_base = NULL;
    stream->_cnt      = 0;
    stream->_fd       = handle->IOD;
    stream->_flag     = (int)stream_flags;
    stream->_tmpfname = NULL;
    
    // associate the stream object
    handle->Stream = stream;
    return EOK;
}

void stdio_handle_destroy(stdio_handle_t* handle)
{
    if (!handle) {
        return;
    }

    hashtable_remove(
            &g_stdioObjects,
            &(struct stdio_object_entry) {
                .id = handle->IOD
            }
    );
    stdio_bitmap_free(handle->IOD);
    free(handle);
}

int stdio_handle_activity(stdio_handle_t* handle , int activity)
{
    oserr_t oserr = OSNotificationQueuePost(&handle->OSHandle, activity);
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        return -1;
    }
    return 0;
}

void stdio_handle_flag(stdio_handle_t* handle, unsigned int flag)
{
    assert(handle != NULL);
    handle->XTFlags |= flag;
}

stdio_handle_t* stdio_handle_get(int iod)
{
    struct stdio_object_entry* result;

    result = hashtable_get(&g_stdioObjects, &(struct stdio_object_entry) { .id = iod });
    if (result != NULL) {
        return result->handle;
    }
    return NULL;
}

FILE* __get_std_handle(int n)
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
    return (handle->XTFlags & WX_TTY) != 0;
}

hashtable_t* stdio_get_handles(void)
{
    return &g_stdioObjects;
}

uuid_t GetNativeHandle(int iod)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    if (!handle) {
        return UUID_INVALID;
    }
    return handle->OSHandle.ID;
}

static uint64_t stdio_hash(const void* element)
{
    const struct stdio_object_entry* entry = element;
    return (uint64_t)entry->id;
}

static int stdio_cmp(const void* element1, const void* element2)
{
    const struct stdio_object_entry* entry1 = element1;
    const struct stdio_object_entry* entry2 = element2;
    return entry1->id == entry2->id ? 0 : -1;
}

void __CleanupSTDIO(void)
{
    // flush all file buffers and close handles
    io_buffer_flush_all(_IOWRT | _IOREAD);

    // close all handles that are not marked _PRIO, and then lastly
    // close the _PRIO handles
    __close_io_descriptors(WX_PRIORITY);
    __close_io_descriptors(0);
}

void StdioInitialize(void)
{
    // initialize the hashtable of handles
    hashtable_construct(
            &g_stdioObjects,
            0,
            sizeof(struct stdio_object_entry),
            stdio_hash, stdio_cmp
    );

    // initialize subsystems for stdio
    stdio_bitmap_initialize();

    // register the cleanup handler
    atexit(__CleanupSTDIO);
}
